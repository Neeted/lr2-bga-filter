using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Principal;
using System.Windows.Forms;
using Microsoft.Win32;

namespace Installer
{
    class Program
    {
        // 定数
        const string DIRECTSHOW_FILTER_CLSID_INSTANCE = @"SOFTWARE\Classes\WOW6432Node\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance";
        const string PREFERRED_FILTERS_KEY = @"SOFTWARE\WOW6432Node\Microsoft\DirectShow\Preferred";

        // LAV Filter GUIDs
        static readonly Dictionary<string, string> LavFilterGuids = new Dictionary<string, string>
        {
            { "{171252A0-8820-4AFE-9DF8-5C92B2D66B04}", "LAV Splitter" },
            { "{B98D13E7-55DB-4385-A33D-09FD1BA26338}", "LAV Splitter Source" },
            { "{EE30215D-164F-4A92-A4EB-9D4C13390F9F}", "LAV Video Decoder" }
        };

        // Merit Values
        const int MERIT_LAV_SPLITTER = unchecked((int)0xff800004);
        const int MERIT_LAV_VIDEO = unchecked((int)0xff800003);

        // Preferred Filters Definitions
        // CodecTweakToolの`Preferred decoders`で設定できる項目を参考にした
        static readonly Dictionary<string, string> PreferredFilterSettings = new Dictionary<string, string>
        {
            // LAV Video Decoder
            { "{41564D57-0000-0010-8000-00AA00389B71}", "{EE30215D-164F-4A92-A4EB-9D4C13390F9F}" }, // Windows Media Video 9, Advanced Profile (non-VC-1-compliant)
            { "{31564D57-0000-0010-8000-00AA00389B71}", "{EE30215D-164F-4A92-A4EB-9D4C13390F9F}" }, // WMV1 (Windows Media Video 7)
            { "{32564D57-0000-0010-8000-00AA00389B71}", "{EE30215D-164F-4A92-A4EB-9D4C13390F9F}" }, // WMV2 (Windows Media Video 8)
            { "{33564D57-0000-0010-8000-00AA00389B71}", "{EE30215D-164F-4A92-A4EB-9D4C13390F9F}" }, // WMV3 (Windows Media Video 9)
            // USE Merit
            { "{31435641-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // AVC1 (No Start Codes, MP4)
            { "{78766964-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // DivX
            { "{58564944-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // DivX 4 (OpenDivX) (Project Mayo)
            { "{34363268-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // h264 (lowercase)
            { "{34363248-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // H264 (Standard)
            { "{3467706D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Microsoft MPEG-4 version 1
            { "{3447504D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Microsoft MPEG-4 version 1
            { "{47504A4D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MJPG (Motion JPEG)
            { "{e436eb80-524f-11ce-9f53-0020af0ba770}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG-1 audio packet ※ こんな名前だけどデフォルトのフィルターはMPEG Video Codec
            { "{7634706D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG-4 Part 2
            { "{5634504D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG-4 Part 2
            { "{7334706D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG-4 Simple Profile
            { "{5334504D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG-4 Simple Profile
            { "{e436eb81-524f-11ce-9f53-0020af0ba770}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG1_Payload
            { "{e06d8026-db46-11cf-b4d1-00805f6cbbea}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG2_VIDEO
            { "{64737664-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // SDL-DVCR (525-60 or 625-50)
            { "{31435657-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // WVC1 (VC-1)
            { "{64697678-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // XviD
            { "{44495658-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // XviD
            { "{3234504D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Microsoft MPEG-4 version 2
            { "{3234706D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Microsoft MPEG-4 version 2
            { "{3334504D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Microsoft MPEG-4 version 3
            { "{3334706D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Microsoft MPEG-4 version 3
            // Windows標準にはないCodecTweakToolで追加されているMediaSubtypeGUID
            { "{31637661-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // avc1 (lowercase)
            { "{43564548-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // HEVC (H.265)
            { "{35363248-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // HEVC / H.265 Video
            { "{31435648-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // HVC1 (HEVC in MP4)
            { "{30357864-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // DivX 5 (dx50)
            { "{30355844-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // DivX 5 (DX50)
            // ここから下の設定はCodecTweakToolでも対象外なので不要かも
            { "{3253534D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MSS2 (WMV9 Screen)
            { "{3153534D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Windows Media Video 7 Screen
            { "{32505657-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Windows Media Video 9 Image v2 (WVP2)
            { "{50564D57-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Windows Media Video 9.1 Image
            { "{52564D57-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // Windows Media Video 9 Image v2 (WMVR) -> LAV Video Decoder
            { "{3253344D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG-4 Advanced Simple Profile
            { "{3273346D-0000-0010-8000-00AA00389B71}", "{ABCD1234-0000-0000-0000-000000000000}" }, // MPEG-4 Advanced Simple Profile
        };

        [STAThread]
        static void Main(string[] args)
        {
            if (!IsAdministrator())
            {
                MessageBox.Show("このアプリケーションを実行するには管理者権限が必要です。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            Console.WriteLine("LR2 BGA Filter Installer / Uninstaller");
            Console.WriteLine("======================================");

            // 簡易的な対話モード
            if (args.Length == 0)
            {
                Console.WriteLine("1. Install");
                Console.WriteLine("2. Uninstall");
                Console.WriteLine("q. Quit");
                Console.Write("Choice: ");
                var key = Console.ReadKey();
                Console.WriteLine();

                if (key.KeyChar == '1') PerformInstall();
                else if (key.KeyChar == '2') PerformUninstall();
                else return;
            }
            else
            {
                if (args[0].ToLower() == "/install") PerformInstall();
                else if (args[0].ToLower() == "/uninstall") PerformUninstall();
            }

            Console.WriteLine("Press any key to exit...");
            Console.ReadKey();
        }

        static void PerformInstall()
        {
            Console.WriteLine("\n[Install Started]");

            // 0. Check for existing installation to prevent overwriting backups
            const string LR2_BGA_FILTER_CLSID = "{61E38596-D44C-4097-89AF-AABBA85DAA6D}";
            if (RegistryHelper.RegistryKeyExists($@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{LR2_BGA_FILTER_CLSID}"))
            {
                MessageBox.Show("LR2 BGA Filter は既にインストールされています。\n二重インストールを防ぐため処理を中止します。\n再インストールが必要な場合は、一度アンインストールを行ってください。", "警告", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            // 1. LAV Check
            Console.WriteLine("Checking LAV Filters...");
            foreach (var kvp in LavFilterGuids)
            {
                string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                if (!RegistryHelper.RegistryKeyExists(keyPath))
                {
                    MessageBox.Show($"LAV Filters (x86) が見つかりません。\n不足コンポーネント: {kvp.Value}\n\nLAVFilters (x86) をインストールしてください。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }
            }

            // 2. Modify LAV Merit
            if (MessageBox.Show("LR2 BGA Filter用にLAV Video Decoder等のメリット値を変更します。\nこれよりレジストリ操作を行います。", "確認", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) != DialogResult.OK) return;

            Console.WriteLine("Backing up LAV settings...");
            foreach (var kvp in LavFilterGuids)
            {
                string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                // Backup e.g. LAV_Splitter_backup.reg
                string safeName = kvp.Value.Replace(" ", "_");
                RegistryHelper.BackupRegistryKey("HKLM", keyPath, Path.GetFullPath($"{safeName}.reg"));
            }

            Console.WriteLine("Modifying LAV Merit values...");
            foreach (var kvp in LavFilterGuids)
            {
                string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                int merit = kvp.Value.Contains("Video") ? unchecked((int)MERIT_LAV_VIDEO) : unchecked((int)MERIT_LAV_SPLITTER);

                // FilterDataバイナリの更新 (Offset 4-7)
                var data = (byte[])RegistryHelper.GetRegistryValue(keyPath, "FilterData");
                if (data != null && data.Length >= 8)
                {
                    byte[] meritBytes = BitConverter.GetBytes(merit); // Little Endian
                    Array.Copy(meritBytes, 0, data, 4, 4);
                    RegistryHelper.SetRegistryValue(keyPath, "FilterData", data, RegistryValueKind.Binary);
                    Console.WriteLine($"Updated Merit for {kvp.Value}");
                }
            }

            // 3. Preferred Filters
            if (MessageBox.Show("Preferred Filter (優先フィルター) の設定を変更します。\n古い互換性プログラムなどで副作用が出る可能性があります。", "確認", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) != DialogResult.OK) return;

            Console.WriteLine("Taking ownership of Preferred key...");
            try
            {
                RegistryHelper.TakeOwnershipAndAllowAccess(PREFERRED_FILTERS_KEY);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Warning: Failed to take ownership: {ex.Message}");
            }

            Console.WriteLine("Backing up Preferred Filter settings...");
            RegistryHelper.BackupRegistryKey("HKLM", PREFERRED_FILTERS_KEY, Path.GetFullPath("Preferred_default.reg"));

            Console.WriteLine("Updating Preferred Filters...");
            foreach (var kvp in PreferredFilterSettings)
            {
                RegistryHelper.SetRegistryValue(PREFERRED_FILTERS_KEY, kvp.Key, kvp.Value, RegistryValueKind.String);
            }

            Console.WriteLine("Restoring ownership to TrustedInstaller...");
            try
            {
                RegistryHelper.ReturnOwnershipToTrustedInstaller(PREFERRED_FILTERS_KEY);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Warning: Failed to restore ownership: {ex.Message}");
            }

            // 4. Register AX
            Console.WriteLine("Registering LR2BGAFilter.ax...");
            string axPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..\\..\\LR2BGAFilter.ax");
            if (!File.Exists(axPath)) axPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LR2BGAFilter.ax"); // fallback

            if (File.Exists(axPath))
            {
                if (FilterRegistrar.RegisterFilter(axPath))
                    Console.WriteLine("Filter Registered Successfully.");
                else
                    Console.WriteLine("Error: Failed to register filter logic.");
            }
            else
            {
                Console.WriteLine($"Error: LR2BGAFilter.ax not found at {axPath}");
            }

            MessageBox.Show("インストールが完了しました。\nLAV Video Decoderの設定で Output Formats の RGB32 のみにチェックを入れてください。\n再起動を推奨します。", "完了", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        static void PerformUninstall()
        {
            Console.WriteLine("\n[Uninstall Started]");

            // 1. Restore LAV
            bool restoredAny = false;
            foreach (var kvp in LavFilterGuids)
            {
                string safeName = kvp.Value.Replace(" ", "_");
                string backupFile = $"{safeName}.reg";
                if (File.Exists(backupFile))
                {
                    if (!restoredAny)
                    {
                        if (MessageBox.Show("LAV Filtersの設定を元に戻しますか？", "確認", MessageBoxButtons.YesNo) != DialogResult.Yes)
                        {
                            break;
                        }
                        restoredAny = true;
                    }
                    RegistryHelper.RestoreRegistryKey(Path.GetFullPath(backupFile));
                    Console.WriteLine($"Restored {kvp.Value} settings.");
                    try { File.Delete(backupFile); } catch { }
                }
            }

            // 2. Restore Preferred
            if (File.Exists("Preferred_default.reg"))
            {
                if (MessageBox.Show("Preferred Filtersの設定を元に戻しますか？", "確認", MessageBoxButtons.YesNo) == DialogResult.Yes)
                {
                    // Need ownership again likely
                    try
                    {
                        RegistryHelper.TakeOwnershipAndAllowAccess(PREFERRED_FILTERS_KEY);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Warning: Failed to take ownership: {ex.Message}");
                    }

                    // Restore to backup state: Delete existing values first (to remove any added keys) then import
                    Console.WriteLine("Cleaning up current Preferred settings...");
                    RegistryHelper.DeleteAllRegistryValues(PREFERRED_FILTERS_KEY);

                    RegistryHelper.RestoreRegistryKey(Path.GetFullPath("Preferred_default.reg"));
                    Console.WriteLine("Restored Preferred settings.");

                    // Restore ownership
                    try
                    {
                        RegistryHelper.ReturnOwnershipToTrustedInstaller(PREFERRED_FILTERS_KEY);
                        Console.WriteLine("Restored ownership to TrustedInstaller.");
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Warning: Failed to restore ownership: {ex.Message}");
                    }

                    // Cleanup backup
                    try { File.Delete("Preferred_default.reg"); } catch { }
                }
            }

            // 3. Unregister AX
            Console.WriteLine("Unregistering LR2BGAFilter.ax...");
            string axPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..\\..\\LR2BGAFilter.ax");
            if (!File.Exists(axPath)) axPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LR2BGAFilter.ax");

            if (File.Exists(axPath))
            {
                if (FilterRegistrar.UnregisterFilter(axPath))
                    Console.WriteLine("Filter Unregistered Successfully.");
            }

            // 4. Cleanup HKCU Settings
            const string USER_SETTINGS_KEY = @"SOFTWARE\LR2BGAFilter";
            if (RegistryHelper.CurrentUserRegistryKeyExists(USER_SETTINGS_KEY))
            {
                if (MessageBox.Show($"フィルタの個別設定が保存されています。\n削除しますか？\n(場所: HKEY_CURRENT_USER\\{USER_SETTINGS_KEY})", "確認", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
                {
                    try
                    {
                        RegistryHelper.DeleteCurrentUserSubKeyTree(USER_SETTINGS_KEY);
                        Console.WriteLine("Deleted user settings.");
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Warning: Failed to delete user settings: {ex.Message}");
                    }
                }
            }

            MessageBox.Show("アンインストールが完了しました。", "完了", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        static bool IsAdministrator()
        {
            var identity = WindowsIdentity.GetCurrent();
            var principal = new WindowsPrincipal(identity);
            return principal.IsInRole(WindowsBuiltInRole.Administrator);
        }
    }
}
