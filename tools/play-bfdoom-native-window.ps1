param(
  [string]$Root = (Split-Path -Parent (Split-Path -Parent $PSCommandPath))
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$rootFull = [System.IO.Path]::GetFullPath($Root)
$wslRoot = (& wsl wslpath -a ($rootFull -replace '\\', '/')).Trim()
$wslScript = ($wslRoot.TrimEnd('/') + "/tools/run-bfdoom-window.sh")

Add-Type -ReferencedAssemblies @("System.Windows.Forms", "System.Drawing") -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

public sealed class BFDoomScreen : Control {
  public BFDoomScreen() {
    SetStyle(ControlStyles.UserPaint |
             ControlStyles.AllPaintingInWmPaint |
             ControlStyles.OptimizedDoubleBuffer |
             ControlStyles.Opaque |
             ControlStyles.ResizeRedraw, true);
    UpdateStyles();
  }

  protected override void OnPaintBackground(PaintEventArgs pevent) {
  }
}

public sealed class BFDoomNativeForm : Form {
  const int FrameWidth = 640;
  const int FrameHeight = 400;
  const int FrameBytes = FrameWidth * FrameHeight * 3;
  const int PacketBytes = 16 + FrameBytes;
  const int TitlebarHeight = 32;
  const int ResizeBorder = 8;
  const int MinScale = 1;
  const int MaxScale = 2;
  const int WM_NCHITTEST = 0x84;
  const int HTCLIENT = 1;
  const int HTLEFT = 10;
  const int HTRIGHT = 11;
  const int HTTOP = 12;
  const int HTTOPLEFT = 13;
  const int HTTOPRIGHT = 14;
  const int HTBOTTOM = 15;
  const int HTBOTTOMLEFT = 16;
  const int HTBOTTOMRIGHT = 17;

  [DllImport("user32.dll")]
  static extern bool ReleaseCapture();

  [DllImport("user32.dll")]
  static extern IntPtr SendMessage(IntPtr hWnd, int msg, int wParam, int lParam);

  readonly Process runner;
  readonly Control screen;
  readonly HashSet<string> held = new HashSet<string>();
  readonly System.Windows.Forms.Timer inputTimer = new System.Windows.Forms.Timer();
  readonly Thread readerThread;
  Bitmap bitmap;
  volatile bool closed;

  public BFDoomNativeForm(string root, string wslScript) {
    Text = "BFDoom";
    FormBorderStyle = FormBorderStyle.None;
    StartPosition = FormStartPosition.CenterScreen;
    ClientSize = new Size(FrameWidth, FrameHeight + TitlebarHeight);
    MinimumSize = new Size(FrameWidth * MinScale, FrameHeight * MinScale + TitlebarHeight);
    MaximumSize = new Size(FrameWidth * MaxScale, FrameHeight * MaxScale + TitlebarHeight);
    BackColor = Color.Black;
    KeyPreview = true;
    DoubleBuffered = true;

    var titlebar = new Panel {
      Dock = DockStyle.Top,
      Height = TitlebarHeight,
      BackColor = Color.FromArgb(18, 18, 18)
    };

    var title = new Label {
      Text = "BFDoom",
      ForeColor = Color.FromArgb(235, 235, 235),
      BackColor = Color.Transparent,
      Font = new Font("Segoe UI", 9f, FontStyle.Regular),
      AutoSize = true,
      Location = new Point(12, 8)
    };

    var close = TitleButton("x", Color.FromArgb(34, 34, 34));
    close.Click += (s, e) => Close();

    var minimize = TitleButton("-", Color.FromArgb(28, 28, 28));
    minimize.Click += (s, e) => WindowState = FormWindowState.Minimized;

    MouseEventHandler drag = (s, e) => {
      if (e.Button == MouseButtons.Left) {
        ReleaseCapture();
        SendMessage(Handle, 0xA1, 0x2, 0);
      }
    };
    titlebar.MouseDown += drag;
    title.MouseDown += drag;

    titlebar.Controls.Add(title);
    titlebar.Controls.Add(close);
    titlebar.Controls.Add(minimize);
    Controls.Add(titlebar);

    screen = new BFDoomScreen {
      Dock = DockStyle.Fill,
      BackColor = Color.Black,
      TabStop = true
    };
    screen.Paint += PaintScreen;
    Controls.Add(screen);
    screen.BringToFront();
    titlebar.BringToFront();

    var psi = new ProcessStartInfo {
      FileName = "wsl",
      Arguments = "bash \"" + wslScript + "\"",
      WorkingDirectory = root,
      UseShellExecute = false,
      RedirectStandardInput = true,
      RedirectStandardOutput = true,
      RedirectStandardError = true,
      CreateNoWindow = true
    };
    runner = Process.Start(psi);

    KeyDown += OnKeyDown;
    KeyUp += OnKeyUp;
    Shown += (s, e) => screen.Focus();
    FormClosed += OnClosed;

    inputTimer.Interval = 16;
    inputTimer.Tick += (s, e) => SendHeldInput();
    inputTimer.Start();

    readerThread = new Thread(ReadFrames) { IsBackground = true };
    readerThread.Start();
  }

  static Button TitleButton(string text, Color color) {
    var button = new Button {
      Text = text,
      Dock = DockStyle.Right,
      Size = new Size(44, TitlebarHeight),
      FlatStyle = FlatStyle.Flat,
      ForeColor = Color.White,
      BackColor = color,
      TabStop = false
    };
    button.FlatAppearance.BorderSize = 0;
    return button;
  }

  void PaintScreen(object sender, PaintEventArgs e) {
    var current = bitmap;
    if (current != null) {
      e.Graphics.Clear(Color.Black);
      e.Graphics.CompositingMode = CompositingMode.SourceCopy;
      e.Graphics.CompositingQuality = CompositingQuality.HighSpeed;
      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      e.Graphics.PixelOffsetMode = PixelOffsetMode.Half;
      e.Graphics.DrawImage(current, AspectFit(screen.ClientRectangle));
    } else {
      e.Graphics.Clear(Color.Black);
    }
  }

  static Rectangle AspectFit(Rectangle bounds) {
    if (bounds.Width <= 0 || bounds.Height <= 0) return bounds;
    double scale = Math.Min(bounds.Width / (double)FrameWidth,
                            bounds.Height / (double)FrameHeight);
    int drawW = Math.Max(1, (int)Math.Round(FrameWidth * scale));
    int drawH = Math.Max(1, (int)Math.Round(FrameHeight * scale));
    return new Rectangle(bounds.X + (bounds.Width - drawW) / 2,
                         bounds.Y + (bounds.Height - drawH) / 2,
                         drawW,
                         drawH);
  }

  protected override void WndProc(ref Message m) {
    base.WndProc(ref m);
    if (m.Msg != WM_NCHITTEST || (int)m.Result != HTCLIENT) return;

    Point p = PointToClient(Cursor.Position);
    bool left = p.X >= 0 && p.X < ResizeBorder;
    bool right = p.X <= ClientSize.Width && p.X >= ClientSize.Width - ResizeBorder;
    bool top = p.Y >= 0 && p.Y < ResizeBorder;
    bool bottom = p.Y <= ClientSize.Height && p.Y >= ClientSize.Height - ResizeBorder;

    if (top && left) m.Result = (IntPtr)HTTOPLEFT;
    else if (top && right) m.Result = (IntPtr)HTTOPRIGHT;
    else if (bottom && left) m.Result = (IntPtr)HTBOTTOMLEFT;
    else if (bottom && right) m.Result = (IntPtr)HTBOTTOMRIGHT;
    else if (left) m.Result = (IntPtr)HTLEFT;
    else if (right) m.Result = (IntPtr)HTRIGHT;
    else if (top) m.Result = (IntPtr)HTTOP;
    else if (bottom) m.Result = (IntPtr)HTBOTTOM;
  }

  void OnKeyDown(object sender, KeyEventArgs e) {
    string mapped = MapKey(e.KeyCode);
    if (mapped.Length == 0) return;
    e.SuppressKeyPress = true;
    if ("wasd".Contains(mapped)) {
      if (mapped == "a") held.Remove("d");
      if (mapped == "d") held.Remove("a");
      if (mapped == "w") held.Remove("s");
      if (mapped == "s") held.Remove("w");
      held.Add(mapped);
    } else {
      SendInput(mapped);
      if (mapped == "q") Close();
    }
  }

  void OnKeyUp(object sender, KeyEventArgs e) {
    string mapped = MapKey(e.KeyCode);
    if (mapped.Length == 0) return;
    e.SuppressKeyPress = true;
    held.Remove(mapped);
  }

  static string MapKey(Keys key) {
    switch (key) {
      case Keys.W:
      case Keys.Up:
        return "w";
      case Keys.S:
      case Keys.Down:
        return "s";
      case Keys.A:
      case Keys.Left:
        return "a";
      case Keys.D:
      case Keys.Right:
        return "d";
      case Keys.Space:
      case Keys.F:
        return "f";
      case Keys.E:
        return "e";
      case Keys.Q:
      case Keys.Escape:
        return "q";
      case Keys.D1:
        return "1";
      case Keys.D2:
        return "2";
      case Keys.D3:
        return "3";
      case Keys.D4:
        return "4";
      case Keys.D5:
        return "5";
      case Keys.D6:
        return "6";
      case Keys.D7:
        return "7";
      default:
        return "";
    }
  }

  void SendHeldInput() {
    var input = new StringBuilder();
    foreach (string key in new[] { "w", "s", "a", "d" }) {
      if (held.Contains(key)) input.Append(key);
    }
    if (input.Length > 0) SendInput(input.ToString());
  }

  void SendInput(string text) {
    if (runner == null || runner.HasExited) return;
    runner.StandardInput.Write(text);
    runner.StandardInput.Flush();
  }

  static void ReadExact(Stream stream, byte[] buffer, int offset, int count) {
    while (count > 0) {
      int n = stream.Read(buffer, offset, count);
      if (n <= 0) throw new EndOfStreamException();
      offset += n;
      count -= n;
    }
  }

  void ReadFrames() {
    var stream = runner.StandardOutput.BaseStream;
    var packet = new byte[PacketBytes];
    var bgra = new byte[FrameWidth * FrameHeight * 4];

    try {
      while (!closed && !runner.HasExited) {
        ReadExact(stream, packet, 0, PacketBytes);
        if (packet[0] != (byte)'B' || packet[1] != (byte)'F' ||
            packet[2] != (byte)'D' || packet[3] != (byte)'W') {
          continue;
        }

        int j = 0;
        for (int i = 16; i < PacketBytes; i += 3) {
          bgra[j] = packet[i + 2];
          bgra[j + 1] = packet[i + 1];
          bgra[j + 2] = packet[i];
          bgra[j + 3] = 255;
          j += 4;
        }

        var next = new Bitmap(FrameWidth, FrameHeight, PixelFormat.Format32bppArgb);
        var rect = new Rectangle(0, 0, FrameWidth, FrameHeight);
        var data = next.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        Marshal.Copy(bgra, 0, data.Scan0, bgra.Length);
        next.UnlockBits(data);

        if (!closed && IsHandleCreated) {
          BeginInvoke((Action)(() => {
            var old = bitmap;
            bitmap = next;
            if (old != null) old.Dispose();
            screen.Invalidate();
          }));
        } else {
          next.Dispose();
        }
      }
    } catch {
      if (!closed && IsHandleCreated) {
        BeginInvoke((Action)(() => Close()));
      }
    }
  }

  void OnClosed(object sender, FormClosedEventArgs e) {
    closed = true;
    inputTimer.Stop();
    try {
      if (runner != null && !runner.HasExited) {
        SendInput("q");
        runner.Kill();
      }
    } catch {}
    if (bitmap != null) bitmap.Dispose();
  }
}

public static class BFDoomNativeWindow {
  public static void Run(string root, string wslScript) {
    Application.EnableVisualStyles();
    Application.SetCompatibleTextRenderingDefault(false);
    Application.Run(new BFDoomNativeForm(root, wslScript));
  }
}
"@

[BFDoomNativeWindow]::Run($rootFull, $wslScript)
