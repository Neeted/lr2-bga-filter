using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Security.Principal;
using Microsoft.Win32;

namespace Installer
{
    public partial class MainForm : Form
    {
        // UI Components for Install Mode
        private CheckBox chkAgreeInstall;
        private Button btnInstall;
        private Label lblInstallWarning;

        // UI Components for Uninstall Mode
        private Label lblBackupStatus;
        private CheckBox chkRestoreLav;
        private CheckBox chkRestorePreferred;
        private CheckBox chkDeleteUserSettings;
        private Button btnUninstall;

        // Models
        private string _axPath;
        private const string LR2_BGA_FILTER_CLSID = "{61E38596-D44C-4097-89AF-AABBA85DAA6D}";
        private const string DIRECTSHOW_FILTER_CLSID_INSTANCE = @"SOFTWARE\Classes\WOW6432Node\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance";
        private const string PREFERRED_FILTERS_KEY = @"SOFTWARE\WOW6432Node\Microsoft\DirectShow\Preferred";

        // LAV Filter GUIDs
        private static readonly Dictionary<string, string> LavFilterGuids = new Dictionary<string, string>
        {
            { "{171252A0-8820-4AFE-9DF8-5C92B2D66B04}", "LAV Splitter" },
            { "{B98D13E7-55DB-4385-A33D-09FD1BA26338}", "LAV Splitter Source" },
            { "{EE30215D-164F-4A92-A4EB-9D4C13390F9F}", "LAV Video Decoder" },
        };

        public MainForm()
        {
            InitializeComponent();
        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            // 管理者権限チェック
            if (!IsAdministrator())
            {
                AppendLog("エラー: 管理者権限が必要です。", true);
                MessageBox.Show("このアプリケーションを実行するには管理者権限が必要です。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Application.Exit();
                return;
            }

            InitializeDynamicUI();
            CheckStateAndSwitchMode();
        }

        private bool IsAdministrator()
        {
            var identity = WindowsIdentity.GetCurrent();
            var principal = new WindowsPrincipal(identity);
            return principal.IsInRole(WindowsBuiltInRole.Administrator);
        }

        private void InitializeDynamicUI()
        {
            // Setup Install Mode UI
            chkAgreeInstall = new CheckBox
            {
                Text = "レジストリ設定の変更に同意する",
                AutoSize = true,
                Location = new Point(10, 10),
                Checked = false
            };
            chkAgreeInstall.CheckedChanged += (s, e) => btnInstall.Enabled = chkAgreeInstall.Checked;

            lblInstallWarning = new Label
            {
                Text = "※インストール時に以下の変更を行います(アンインストール時に復元可能):\n- LAV Filters のメリット値(優先度)の変更\n- DirectShow Preferred Filters (最優先フィルタ) の設定変更",
                AutoSize = true,
                Location = new Point(10, 40),
                ForeColor = Color.Red
            };

            btnInstall = new Button
            {
                Text = "インストール",
                Location = new Point(400, 10),
                Size = new Size(120, 40),
                Enabled = false
            };
            btnInstall.Click += BtnInstall_Click;


            // Setup Uninstall Mode UI
            lblBackupStatus = new Label
            {
                Text = "バックアップ状態を確認中...",
                AutoSize = true,
                Location = new Point(10, 5)
            };

            chkRestoreLav = new CheckBox
            {
                Text = "LAV Filtersの設定を元に戻す",
                AutoSize = true,
                Location = new Point(10, 30),
                Checked = true
            };

            chkRestorePreferred = new CheckBox
            {
                Text = "Preferred Filtersの設定を元に戻す",
                AutoSize = true,
                Location = new Point(10, 55),
                Checked = true
            };

            chkDeleteUserSettings = new CheckBox
            {
                Text = "LR2 BGA Filterの設定(HKCU)を削除する",
                AutoSize = true,
                Location = new Point(10, 80),
                Checked = false
            };

            btnUninstall = new Button
            {
                Text = "アンインストール",
                Location = new Point(400, 10),
                Size = new Size(120, 40)
            };
            btnUninstall.Click += BtnUninstall_Click;
        }

        private void CheckStateAndSwitchMode()
        {
            pnlActions.Controls.Clear();
            txtLog.Text = "";

            // Check if installed by Registry check of specific CLSID
            bool isInstalled = RegistryHelper.RegistryKeyExists($@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{LR2_BGA_FILTER_CLSID}");

            if (!isInstalled)
            {
                SetupInstallMode();
            }
            else
            {
                SetupUninstallMode();
            }
        }

        private void SetupInstallMode()
        {
            lblTitle.Text = "LR2 BGA Filter インストール";
            pnlActions.Controls.Add(chkAgreeInstall);
            pnlActions.Controls.Add(lblInstallWarning);
            pnlActions.Controls.Add(btnInstall);

            AppendLog("インストーラーを起動しました。");
            AppendLog("インストールを開始するには、変更内容に同意して「インストール」をクリックしてください。");

            // Filter AX Path
            CheckFilterFile();
            if (pnlActions.Enabled)
            {
                CheckLavFilters();
            }
        }

        private void CheckFilterFile()
        {
            AppendLog("LR2BGAFilter.ax の存在確認中...");

            // Expected to be in the same directory as the installer
            string axPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LR2BGAFilter.ax");

            if (File.Exists(axPath))
            {
                _axPath = axPath;
                AppendLog($"LR2BGAFilter.ax が確認されました: {Path.GetFileName(axPath)}");
            }
            else
            {
                AppendLog($"[エラー] LR2BGAFilter.ax が見つかりません。", true);
                AppendLog($"探索パス: {axPath}", true);
                MessageBox.Show("LR2BGAFilter.ax が見つかりません。\nインストーラーと同じフォルダに配置してください。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
                pnlActions.Enabled = false;
            }
        }

        private void SetupUninstallMode()
        {
            lblTitle.Text = "LR2 BGA Filter アンインストール";
            pnlActions.Controls.Add(lblBackupStatus);
            pnlActions.Controls.Add(chkRestoreLav);
            pnlActions.Controls.Add(chkRestorePreferred);
            pnlActions.Controls.Add(chkDeleteUserSettings);
            pnlActions.Controls.Add(btnUninstall);

            AppendLog("アンインストーラーを起動しました。");

            // For Uninstall, we just need to know if the file exists to unregister it, but even if it's gone we should proceed with cleanup.
            // But let's find the path if we can.
            string axPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LR2BGAFilter.ax");
            _axPath = axPath;

            // Check backups
            bool lavBackupExists = LavFilterGuids.Any(kvp => File.Exists($"{kvp.Value.Replace(" ", "_")}.reg"));
            bool prefBackupExists = File.Exists("Preferred_default.reg");

            chkRestoreLav.Enabled = lavBackupExists;
            chkRestorePreferred.Enabled = prefBackupExists;

            if (!lavBackupExists) chkRestoreLav.Checked = false;
            if (!prefBackupExists) chkRestorePreferred.Checked = false;

            lblBackupStatus.Text = $"バックアップ: LAV={(lavBackupExists ? "あり" : "なし")}, Preferred={(prefBackupExists ? "あり" : "なし")}";
        }

        private void CheckLavFilters()
        {
            AppendLog("LAV Filters (x86) の存在確認中...");
            bool missing = false;
            foreach (var kvp in LavFilterGuids)
            {
                string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                if (!RegistryHelper.RegistryKeyExists(keyPath))
                {
                    AppendLog($"[エラー] {kvp.Value} が見つかりません。", true);
                    missing = true;
                }
            }

            if (missing)
            {
                AppendLog("LAV Filters (x86) がインストールされていないため、処理を続行できません。", true);
                MessageBox.Show("LAV Filters (x86) が見つかりません。\nLAVFilters (x86) をインストールしてから再実行してください。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
                // Disable UI
                pnlActions.Enabled = false;
            }
            else
            {
                AppendLog("LAV Filters (x86) が確認されました。");
            }
        }

        private async void BtnInstall_Click(object sender, EventArgs e)
        {
            pnlActions.Enabled = false;
            AppendLog("\n[インストール開始]");

            await Task.Run(() => PerformInstall());

            pnlActions.Enabled = true;
            MessageBox.Show("インストールが完了しました。\n再起動を推奨します。", "完了", MessageBoxButtons.OK, MessageBoxIcon.Information);

            ShowCompletionUI();
        }

        private async void BtnUninstall_Click(object sender, EventArgs e)
        {
            pnlActions.Enabled = false;
            AppendLog("\n[アンインストール開始]");

            bool restoreLav = chkRestoreLav.Checked;
            bool restorePref = chkRestorePreferred.Checked;
            bool deleteUser = chkDeleteUserSettings.Checked;

            await Task.Run(() => PerformUninstall(restoreLav, restorePref, deleteUser));

            pnlActions.Enabled = true;

            MessageBox.Show("アンインストールが完了しました。", "完了", MessageBoxButtons.OK, MessageBoxIcon.Information);

            ShowCompletionUI();
        }

        private void ShowCompletionUI()
        {
            pnlActions.Controls.Clear();

            var btnExit = new Button
            {
                Text = "終了",
                Location = new Point((pnlActions.Width - 120) / 2, 30), // Center
                Size = new Size(120, 40)
            };
            btnExit.Click += (s, e) => Application.Exit();

            pnlActions.Controls.Add(btnExit);
            AppendLog("\n[全ての処理が完了しました。終了ボタンを押して閉じてください。]");
        }

        private void AppendLog(string text, bool error = false)
        {
            if (InvokeRequired)
            {
                Invoke(new Action(() => AppendLog(text, error)));
                return;
            }

            txtLog.AppendText(text + Environment.NewLine);
            // Optionally color error lines (RichTextBox would be better but TextBox is simple)
            // Just prefix with [Error] if needed as handled in caller
        }

        // Logic methods
        private void PerformInstall()
        {
            try
            {
                // 1. LAV Check (Already done at startup, but double check)
                AppendLog("Checking LAV Filters...");
                foreach (var kvp in LavFilterGuids)
                {
                    string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                    if (!RegistryHelper.RegistryKeyExists(keyPath))
                    {
                        AppendLog($"[エラー] {kvp.Value} が見つかりません。", true);
                        MessageBox.Show($"LAV Filters (x86) が見つかりません。\n不足コンポーネント: {kvp.Value}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
                        return;
                    }
                }

                // 2. Modify LAV Merit
                if (chkAgreeInstall.Checked)
                {
                    AppendLog("Backing up LAV settings...");
                    foreach (var kvp in LavFilterGuids)
                    {
                        string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                        string safeName = kvp.Value.Replace(" ", "_");
                        RegistryHelper.BackupRegistryKey("HKLM", keyPath, Path.GetFullPath($"{safeName}.reg"));
                    }

                    AppendLog("Modifying LAV Merit values...");
                    // Merit Values
                    const int MERIT_LAV_SPLITTER = unchecked((int)0xff800004);
                    const int MERIT_LAV_VIDEO = unchecked((int)0xff800003);

                    foreach (var kvp in LavFilterGuids)
                    {
                        string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                        int merit = kvp.Value.Contains("Video") ? MERIT_LAV_VIDEO : MERIT_LAV_SPLITTER;

                        var data = (byte[])RegistryHelper.GetRegistryValue(keyPath, "FilterData");
                        if (data != null && data.Length >= 8)
                        {
                            byte[] meritBytes = BitConverter.GetBytes(merit);
                            Array.Copy(meritBytes, 0, data, 4, 4);
                            RegistryHelper.SetRegistryValue(keyPath, "FilterData", data, RegistryValueKind.Binary);
                            AppendLog($"Updated Merit for {kvp.Value}");
                        }
                    }

                    // 3. Preferred Filters
                    AppendLog("Taking ownership of Preferred key...");
                    try
                    {
                        RegistryHelper.TakeOwnershipAndAllowAccess(PREFERRED_FILTERS_KEY);
                    }
                    catch (Exception ex)
                    {
                        AppendLog($"[警告] Failed to take ownership: {ex.Message}", true);
                    }

                    AppendLog("Backing up Preferred Filter settings...");
                    RegistryHelper.BackupRegistryKey("HKLM", PREFERRED_FILTERS_KEY, Path.GetFullPath("Preferred_default.reg"));

                    AppendLog("Updating Preferred Filters...");
                    var PreferredFilterSettings = new Dictionary<string, string>
                    {
                        // CodecTweakToolの`Preferred decoders`で設定できる項目を参考にした
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

                    foreach (var kvp in PreferredFilterSettings)
                    {
                        RegistryHelper.SetRegistryValue(PREFERRED_FILTERS_KEY, kvp.Key, kvp.Value, RegistryValueKind.String);
                    }

                    AppendLog("Restoring ownership to TrustedInstaller...");
                    try
                    {
                        RegistryHelper.ReturnOwnershipToTrustedInstaller(PREFERRED_FILTERS_KEY);
                    }
                    catch (Exception ex)
                    {
                        AppendLog($"[警告] Failed to restore ownership: {ex.Message}", true);
                    }
                }

                // 4. Register AX
                AppendLog("Registering LR2BGAFilter.ax...");

                if (File.Exists(_axPath))
                {
                    if (FilterRegistrar.RegisterFilter(_axPath))
                        AppendLog("Filter Registered Successfully.");
                    else
                        AppendLog("[エラー] Failed to register filter logic.", true);
                }
                else
                {
                    AppendLog($"[エラー] LR2BGAFilter.ax not found at {_axPath}", true);
                }
            }
            catch (Exception ex)
            {
                AppendLog($"[致命的エラー] {ex.Message}\n{ex.StackTrace}", true);
                MessageBox.Show($"予期しないエラーが発生しました:\n{ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void PerformUninstall(bool restoreLav, bool restorePref, bool deleteUser)
        {
            try
            {
                // 1. Restore LAV
                if (restoreLav)
                {
                    foreach (var kvp in LavFilterGuids)
                    {
                        string safeName = kvp.Value.Replace(" ", "_");
                        string backupFile = $"{safeName}.reg";
                        if (File.Exists(backupFile))
                        {
                            RegistryHelper.RestoreRegistryKey(Path.GetFullPath(backupFile));
                            AppendLog($"Restored {kvp.Value} settings.");
                            try { File.Delete(backupFile); } catch { }
                        }
                    }
                }

                // 2. Restore Preferred
                if (restorePref && File.Exists("Preferred_default.reg"))
                {
                    // Need ownership again likely
                    try
                    {
                        RegistryHelper.TakeOwnershipAndAllowAccess(PREFERRED_FILTERS_KEY);
                    }
                    catch (Exception ex)
                    {
                        AppendLog($"[警告] Failed to take ownership: {ex.Message}", true);
                    }

                    // Restore to backup state: Delete existing values first (to remove any added keys) then import
                    AppendLog("Cleaning up current Preferred settings...");
                    RegistryHelper.DeleteAllRegistryValues(PREFERRED_FILTERS_KEY);

                    RegistryHelper.RestoreRegistryKey(Path.GetFullPath("Preferred_default.reg"));
                    AppendLog("Restored Preferred settings.");

                    // Restore ownership
                    try
                    {
                        RegistryHelper.ReturnOwnershipToTrustedInstaller(PREFERRED_FILTERS_KEY);
                        AppendLog("Restored ownership to TrustedInstaller.");
                    }
                    catch (Exception ex)
                    {
                        AppendLog($"[警告] Failed to restore ownership: {ex.Message}", true);
                    }

                    // Cleanup backup
                    try { File.Delete("Preferred_default.reg"); } catch { }
                }

                // 3. Unregister AX
                AppendLog("Unregistering LR2BGAFilter.ax...");

                if (File.Exists(_axPath))
                {
                    if (FilterRegistrar.UnregisterFilter(_axPath))
                        AppendLog("Filter Unregistered Successfully.");
                }

                // 4. Cleanup HKCU Settings
                if (deleteUser)
                {
                    const string USER_SETTINGS_KEY = @"SOFTWARE\LR2BGAFilter";
                    if (RegistryHelper.CurrentUserRegistryKeyExists(USER_SETTINGS_KEY))
                    {
                        try
                        {
                            RegistryHelper.DeleteCurrentUserSubKeyTree(USER_SETTINGS_KEY);
                            AppendLog("Deleted user settings.");
                        }
                        catch (Exception ex)
                        {
                            AppendLog($"[警告] Failed to delete user settings: {ex.Message}", true);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                AppendLog($"[致命的エラー] {ex.Message}\n{ex.StackTrace}", true);
                MessageBox.Show($"予期しないエラーが発生しました:\n{ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
