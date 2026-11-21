using System;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;

public static class IsoduoRealInput
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left, Top, Right, Bottom;
        public int Width { get { return Right - Left; } }
        public int Height { get { return Bottom - Top; } }
    }

    [StructLayout(LayoutKind.Sequential)]
    struct Point { public int X, Y; }

    [StructLayout(LayoutKind.Sequential)]
    struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }

    [StructLayout(LayoutKind.Sequential)]
    struct Input { public uint Type; public Payload Data; }

    [StructLayout(LayoutKind.Explicit)]
    struct Payload
    {
        [FieldOffset(0)] public MouseInput Mouse;
        [FieldOffset(0)] public KeyInput Key;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MouseInput { public int X, Y, Data; public uint Flags, Time; public UIntPtr Extra; }

    [StructLayout(LayoutKind.Sequential)]
    struct KeyInput { public ushort Key, Scan; public uint Flags, Time; public UIntPtr Extra; }

    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, IntPtr pid);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool PostMessageW(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", SetLastError = true)] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", SetLastError = true)] static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", SetLastError = true)] static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", SetLastError = true)] static extern bool GetMonitorInfoW(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint count, Input[] inputs, int size);

    public static Rect WindowRect(IntPtr window)
    {
        Rect rect;
        if (!GetWindowRect(window, out rect)) throw new Win32Exception();
        return rect;
    }

    public static Rect ClientRect(IntPtr window)
    {
        Rect rect;
        var origin = new Point();
        if (!GetClientRect(window, out rect) || !ClientToScreen(window, ref origin)) throw new Win32Exception();
        rect.Right += origin.X;
        rect.Bottom += origin.Y;
        rect.Left = origin.X;
        rect.Top = origin.Y;
        return rect;
    }

    public static void FitWindow(IntPtr window, int requestedWidth)
    {
        var monitor = new MonitorInfo { Size = Marshal.SizeOf(typeof(MonitorInfo)) };
        if (!GetMonitorInfoW(MonitorFromWindow(window, 2), ref monitor)) throw new Win32Exception();
        var current = WindowRect(window);
        var width = Math.Min(requestedWidth > 0 ? requestedWidth : current.Width, monitor.Work.Width);
        var height = Math.Min(current.Height, monitor.Work.Height);
        var x = monitor.Work.Left + (monitor.Work.Width - width) / 2;
        var y = monitor.Work.Top + (monitor.Work.Height - height) / 2;
        if (!SetWindowPos(window, IntPtr.Zero, x, y, width, height, 0x0014)) throw new Win32Exception();
    }

    public static void Guard(IntPtr window)
    {
        if (GetForegroundWindow() != window) throw new InvalidOperationException("Demo lost foreground focus");
    }

    public static void Activate(IntPtr window)
    {
        SetForegroundWindow(window);
        Thread.Sleep(150);
        if (GetForegroundWindow() == window) return;
        // If foreground activation is denied, click the caption only when it belongs to this demo.
        var rect = WindowRect(window);
        var client = ClientRect(window);
        var p = new Point { X = rect.Left + rect.Width / 2, Y = (rect.Top + client.Top) / 2 };
        if (WindowFromPoint(p) == window)
        {
            Move(p.X, p.Y);
            Mouse(2);
            Mouse(4);
        }
        Thread.Sleep(150);
        Guard(window);
    }

    static void Send(params Input[] inputs)
    {
        if (SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(Input))) != inputs.Length)
            throw new Win32Exception();
    }

    static void KeyPair(ushort key, ushort scan, uint flags)
    {
        Send(new Input { Type = 1, Data = new Payload { Key = new KeyInput { Key = key, Scan = scan, Flags = flags } } },
             new Input { Type = 1, Data = new Payload { Key = new KeyInput { Key = key, Scan = scan, Flags = flags | 2 } } });
    }

    public static void Tap(IntPtr window, ushort key)
    {
        Guard(window);
        Send(new Input { Type = 1, Data = new Payload { Key = new KeyInput { Key = key } } });
        Thread.Sleep(30);
        Send(new Input { Type = 1, Data = new Payload { Key = new KeyInput { Key = key, Flags = 2 } } });
        Thread.Sleep(30);
    }

    public static void Type(IntPtr window, string text)
    {
        foreach (var character in text)
        {
            Guard(window);
            KeyPair(0, character, 4);
            Thread.Sleep(20);
        }
    }

    public static void Mouse(uint flags, int data = 0)
    {
        Send(new Input { Data = new Payload { Mouse = new MouseInput { Flags = flags, Data = data } } });
    }

    public static void Move(int x, int y)
    {
        if (!SetCursorPos(x, y)) throw new Win32Exception();
    }

    public static void Click(IntPtr window, int x, int y, bool right)
    {
        Guard(window);
        if (WindowFromPoint(new Point { X = x, Y = y }) != window)
            throw new InvalidOperationException("Click point is outside the visible demo window");
        Move(x, y);
        Mouse(right ? 8u : 2u);
        Thread.Sleep(40);
        Mouse(right ? 16u : 4u);
        Thread.Sleep(250);
    }

    static Bitmap Capture(Rect rect)
    {
        var bitmap = new Bitmap(rect.Width, rect.Height);
        try
        {
            using (var graphics = Graphics.FromImage(bitmap))
                graphics.CopyFromScreen(rect.Left, rect.Top, 0, 0, bitmap.Size);
            return bitmap;
        }
        catch
        {
            bitmap.Dispose();
            throw;
        }
    }

    public static void Screenshot(IntPtr window, string path)
    {
        using (var bitmap = Capture(WindowRect(window)))
            bitmap.Save(path, ImageFormat.Png);
    }

    public static int[] ThumbAtEnd(IntPtr window, bool bottom)
    {
        Guard(window);
        var client = ClientRect(window);
        // The page track is 10 DIP wide and spans the right edge of the client.
        // Sample its center column, just inside the rounded cap at either track end.
        var dpi = GetDpiForWindow(window);
        var x = client.Right - (int)Math.Round(5 * dpi / 96.0);
        var end = bottom ? client.Bottom - 1 : client.Top;
        var direction = bottom ? -1 : 1;
        var inset = (int)Math.Round(8 * dpi / 96.0);
        var start = end + direction * inset;
        var strip = new Rect { Left = x, Right = x + 1, Top = client.Top, Bottom = client.Bottom };
        using (var bitmap = Capture(strip))
        {
            var color = bitmap.GetPixel(0, start - client.Top);
            var length = 1;
            while (length + inset < client.Height && bitmap.GetPixel(0, start - client.Top + length * direction) == color)
                ++length;
            if (length < inset || length + inset >= client.Height)
                throw new InvalidOperationException("Page scrollbar thumb could not be located at the track end");
            return new[] { x, end + direction * ((length + inset) / 2) };
        }
    }
}
