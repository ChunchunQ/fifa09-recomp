using System.Windows.Input;

namespace Fifa09Launcher;

/// <summary>
/// Keyboard-to-controller bindings. Keys are the suffix of the SDK cvar
/// (keybind_&lt;key&gt;), values are SDK key names, comma separated for multiple keys.
/// </summary>
public static class Keybind
{
    // (cvar suffix, label shown in the launcher)
    public static readonly (string Id, string Label)[] All =
    {
        ("lstick_up",      "Move up"),
        ("lstick_down",    "Move down"),
        ("lstick_left",    "Move left"),
        ("lstick_right",   "Move right"),
        ("a",              "Short pass / tackle (A)"),
        ("b",              "Shoot (B)"),
        ("x",              "Lob / cross (X)"),
        ("y",              "Through ball (Y)"),
        ("right_trigger",  "Sprint (RT)"),
        ("left_trigger",   "Left trigger (LT)"),
        ("left_shoulder",  "Left bumper (LB)"),
        ("right_shoulder", "Right bumper (RB)"),
        ("lstick_press",   "Left stick press"),
        ("rstick_up",      "Skill stick up"),
        ("rstick_down",    "Skill stick down"),
        ("rstick_left",    "Skill stick left"),
        ("rstick_right",   "Skill stick right"),
        ("rstick_press",   "Right stick press"),
        ("dpad_up",        "D-pad up"),
        ("dpad_down",      "D-pad down"),
        ("dpad_left",      "D-pad left"),
        ("dpad_right",     "D-pad right"),
        ("start",          "Start / pause"),
        ("back",           "Back"),
    };

    public static readonly Dictionary<string, string> Defaults = new()
    {
        ["lstick_up"] = "W", ["lstick_down"] = "S", ["lstick_left"] = "A", ["lstick_right"] = "D",
        ["a"] = "J", ["b"] = "K", ["x"] = "L", ["y"] = "I",
        ["right_trigger"] = "Space", ["left_trigger"] = "U",
        ["left_shoulder"] = "Q", ["right_shoulder"] = "E",
        ["lstick_press"] = "F",
        ["rstick_up"] = "T", ["rstick_down"] = "G", ["rstick_left"] = "R", ["rstick_right"] = "Y",
        ["rstick_press"] = "H",
        ["dpad_up"] = "1", ["dpad_down"] = "2", ["dpad_left"] = "3", ["dpad_right"] = "4",
        ["start"] = "P", ["back"] = "Backspace",
    };

    /// <summary>
    /// Maps a WPF key to the SDK key name (table in rexglue-sdk src/ui/keybinds.cpp),
    /// or null when the SDK has no name for it.
    /// </summary>
    public static string? ToSdkName(Key key)
    {
        if (key >= Key.A && key <= Key.Z) return key.ToString();
        if (key >= Key.D0 && key <= Key.D9) return ((int)(key - Key.D0)).ToString();
        if (key >= Key.NumPad0 && key <= Key.NumPad9) return "Numpad" + (int)(key - Key.NumPad0);
        if (key >= Key.F1 && key <= Key.F24) return key.ToString();
        return key switch
        {
            Key.Space => "Space",
            Key.Back => "Backspace",
            Key.Return => "Return",
            Key.Tab => "Tab",
            Key.Delete => "Delete",
            Key.Insert => "Insert",
            Key.Home => "Home",
            Key.End => "End",
            Key.PageUp => "PageUp",
            Key.PageDown => "PageDown",
            Key.Left => "Left",
            Key.Right => "Right",
            Key.Up => "Up",
            Key.Down => "Down",
            Key.LeftShift or Key.RightShift => "Shift",
            Key.LeftCtrl or Key.RightCtrl => "Control",
            Key.LeftAlt or Key.RightAlt => "Alt",
            Key.OemTilde => "Backtick",
            Key.OemMinus => "Minus",
            Key.OemPlus => "Plus",
            Key.OemComma => "Comma",
            Key.OemPeriod => "Period",
            Key.OemSemicolon => "Semicolon",
            Key.OemQuestion => "Slash",
            Key.OemPipe or Key.OemBackslash => "Backslash",
            Key.OemOpenBrackets => "LBracket",
            Key.OemCloseBrackets => "RBracket",
            Key.OemQuotes => "Quote",
            Key.Add => "NumpadPlus",
            Key.Subtract => "NumpadMinus",
            Key.Multiply => "NumpadStar",
            Key.Divide => "NumpadSlash",
            Key.CapsLock => "CapsLock",
            _ => null,
        };
    }
}
