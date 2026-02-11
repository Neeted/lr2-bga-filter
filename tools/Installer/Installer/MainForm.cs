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

        // Localization
        private static System.Resources.ResourceManager _resourceManager;
        private static System.Resources.ResourceManager _defaultResourceManager;

        private string GetString(string name)
        {
            if (_resourceManager == null)
            {
                string baseName = "Installer.Resources.Strings";
                var lang = System.Globalization.CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;

                if (lang == "ja") baseName = "Installer.Resources.Strings_ja";
                else if (lang == "ko") baseName = "Installer.Resources.Strings_ko";

                _resourceManager = new System.Resources.ResourceManager(baseName, typeof(MainForm).Assembly);
            }

            try
            {
                string val = _resourceManager.GetString(name);
                if (!string.IsNullOrEmpty(val)) return val;
            }
            catch { }

            // Fallback to default (English)
            if (_defaultResourceManager == null)
            {
                _defaultResourceManager = new System.Resources.ResourceManager("Installer.Resources.Strings", typeof(MainForm).Assembly);
            }

            try
            {
                return _defaultResourceManager.GetString(name);
            }
            catch
            {
                return null;
            }
        }

        public MainForm()
        {
            InitializeComponent();
        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            // 管理者権限チェック
            if (!IsAdministrator())
            {
                AppendLog(GetString("Error_AdminRequired"), true);
                MessageBox.Show(GetString("Msg_AdminRequired"), GetString("Msg_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error);
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
                Text = GetString("Check_Agree"),
                AutoSize = true,
                Location = new Point(10, 10),
                Checked = false
            };
            chkAgreeInstall.CheckedChanged += (s, e) => btnInstall.Enabled = chkAgreeInstall.Checked;

            lblInstallWarning = new Label
            {
                Text = GetString("Warning_Install"),
                AutoSize = true,
                Location = new Point(10, 40),
                ForeColor = Color.Red
            };

            btnInstall = new Button
            {
                Text = GetString("Button_Install"),
                Location = new Point(400, 10),
                Size = new Size(120, 40),
                Enabled = false
            };
            btnInstall.Click += BtnInstall_Click;


            // Setup Uninstall Mode UI
            lblBackupStatus = new Label
            {
                Text = GetString("Label_BackupStatus"),
                AutoSize = true,
                Location = new Point(10, 5)
            };

            chkRestoreLav = new CheckBox
            {
                Text = GetString("Check_RestoreLav"),
                AutoSize = true,
                Location = new Point(10, 30),
                Checked = true
            };

            chkRestorePreferred = new CheckBox
            {
                Text = GetString("Check_RestorePreferred"),
                AutoSize = true,
                Location = new Point(10, 55),
                Checked = true
            };

            chkDeleteUserSettings = new CheckBox
            {
                Text = GetString("Check_DeleteUser"),
                AutoSize = true,
                Location = new Point(10, 80),
                Checked = false
            };

            btnUninstall = new Button
            {
                Text = GetString("Button_Uninstall"),
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
            lblTitle.Text = GetString("Title_Install");
            pnlActions.Controls.Add(chkAgreeInstall);
            pnlActions.Controls.Add(lblInstallWarning);
            pnlActions.Controls.Add(btnInstall);

            AppendLog(GetString("Log_InstallerStarted"));
            AppendLog(GetString("Log_StartInstall"));

            // Filter AX Path
            CheckFilterFile();
            if (pnlActions.Enabled)
            {
                CheckLavFilters();
            }
        }

        private void CheckFilterFile()
        {
            AppendLog(GetString("Log_CheckAx"));

            // Expected to be in the same directory as the installer
            string axPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LR2BGAFilter.ax");

            if (File.Exists(axPath))
            {
                _axPath = axPath;
                AppendLog(string.Format(GetString("Log_AxFound"), Path.GetFileName(axPath)));
            }
            else
            {
                AppendLog(GetString("Error_AxNotFound"), true);
                AppendLog(string.Format(GetString("Log_SearchPath"), axPath), true);
                MessageBox.Show(GetString("Msg_AxNotFound_Body"), GetString("Msg_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error);
                pnlActions.Enabled = false;
            }
        }

        private void SetupUninstallMode()
        {
            lblTitle.Text = GetString("Title_Uninstall");
            pnlActions.Controls.Add(lblBackupStatus);
            pnlActions.Controls.Add(chkRestoreLav);
            pnlActions.Controls.Add(chkRestorePreferred);
            pnlActions.Controls.Add(chkDeleteUserSettings);
            pnlActions.Controls.Add(btnUninstall);

            AppendLog(GetString("Log_UninstallerStarted"));

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

            lblBackupStatus.Text = string.Format(GetString("Log_ShowBackupStatus"),
                lavBackupExists ? GetString("Status_Exists") : GetString("Status_NotExists"),
                prefBackupExists ? GetString("Status_Exists") : GetString("Status_NotExists"));
        }

        private void CheckLavFilters()
        {
            AppendLog(GetString("Log_CheckLav"));
            bool missing = false;
            foreach (var kvp in LavFilterGuids)
            {
                string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                if (!RegistryHelper.RegistryKeyExists(keyPath))
                {
                    AppendLog(string.Format(GetString("Error_LavNotFound"), kvp.Value), true);
                    missing = true;
                }
            }

            if (missing)
            {
                AppendLog(GetString("Msg_LavNotFound_Body2"), true);
                MessageBox.Show(GetString("Msg_LavNotFound_Body2"), GetString("Msg_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error);
                // Disable UI
                pnlActions.Enabled = false;
            }
            else
            {
                AppendLog(GetString("Log_LavFound"));
            }
        }

        private async void BtnInstall_Click(object sender, EventArgs e)
        {
            pnlActions.Enabled = false;
            AppendLog(Environment.NewLine + GetString("Log_InstallBegin"));

            await Task.Run(() => PerformInstall());

            pnlActions.Enabled = true;
            MessageBox.Show(GetString("Msg_InstallComplete"), GetString("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information);

            ShowCompletionUI();
        }

        private async void BtnUninstall_Click(object sender, EventArgs e)
        {
            pnlActions.Enabled = false;
            AppendLog(Environment.NewLine + GetString("Log_UninstallBegin"));

            bool restoreLav = chkRestoreLav.Checked;
            bool restorePref = chkRestorePreferred.Checked;
            bool deleteUser = chkDeleteUserSettings.Checked;

            await Task.Run(() => PerformUninstall(restoreLav, restorePref, deleteUser));

            pnlActions.Enabled = true;

            MessageBox.Show(GetString("Msg_UninstallComplete"), GetString("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information);

            ShowCompletionUI();
        }

        private void ShowCompletionUI()
        {
            pnlActions.Controls.Clear();

            var btnExit = new Button
            {
                Text = GetString("Button_Exit"),
                Location = new Point((pnlActions.Width - 120) / 2, 30), // Center
                Size = new Size(120, 40)
            };
            btnExit.Click += (s, e) => Application.Exit();

            pnlActions.Controls.Add(btnExit);
            AppendLog(Environment.NewLine + GetString("Log_Complete"));
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
                AppendLog(GetString("Log_CheckLav"));
                foreach (var kvp in LavFilterGuids)
                {
                    string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                    if (!RegistryHelper.RegistryKeyExists(keyPath))
                    {
                        AppendLog(string.Format(GetString("Error_LavNotFound"), kvp.Value), true);
                        MessageBox.Show(string.Format(GetString("Msg_LavNotFound_Body"), kvp.Value), GetString("Msg_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error);
                        return;
                    }
                }

                // 2. Modify LAV Merit
                if (chkAgreeInstall.Checked)
                {
                    AppendLog(GetString("Log_BackupLav"));
                    foreach (var kvp in LavFilterGuids)
                    {
                        string keyPath = $@"{DIRECTSHOW_FILTER_CLSID_INSTANCE}\{kvp.Key}";
                        string safeName = kvp.Value.Replace(" ", "_");
                        RegistryHelper.BackupRegistryKey("HKLM", keyPath, Path.GetFullPath($"{safeName}.reg"));
                    }

                    AppendLog(GetString("Log_ModifyLav"));
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
                            AppendLog(string.Format(GetString("Log_UpdateMerit"), kvp.Value));
                        }
                    }

                    // 3. Preferred Filters
                    AppendLog(GetString("Log_TakeOwnership"));
                    try
                    {
                        RegistryHelper.TakeOwnershipAndAllowAccess(PREFERRED_FILTERS_KEY);
                    }
                    catch (Exception ex)
                    {
                        AppendLog(string.Format(GetString("Warning_TakeOwnershipFailed"), ex.Message), true);
                    }

                    AppendLog(GetString("Log_BackupPreferred"));
                    RegistryHelper.BackupRegistryKey("HKLM", PREFERRED_FILTERS_KEY, Path.GetFullPath("Preferred_default.reg"));

                    AppendLog(GetString("Log_UpdatePreferred"));
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

                    AppendLog(GetString("Log_RestoreOwnership"));
                    try
                    {
                        RegistryHelper.ReturnOwnershipToTrustedInstaller(PREFERRED_FILTERS_KEY);
                    }
                    catch (Exception ex)
                    {
                        AppendLog(string.Format(GetString("Warning_RestoreOwnershipFailed"), ex.Message), true);
                    }
                }

                // 4. Register AX
                AppendLog(GetString("Log_RegisterAx"));

                if (File.Exists(_axPath))
                {
                    if (FilterRegistrar.RegisterFilter(_axPath))
                        AppendLog(GetString("Log_RegisterSuccess"));
                    else
                        AppendLog(GetString("Error_RegisterFailed"), true);
                }
                else
                {
                    AppendLog(string.Format(GetString("Error_AxNotFound"), _axPath), true);
                }
            }
            catch (Exception ex)
            {
                AppendLog(string.Format(GetString("Error_Unexpected"), ex.Message + "\n" + ex.StackTrace), true);
                MessageBox.Show(string.Format(GetString("Error_Unexpected"), ex.Message), GetString("Msg_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error);
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
                            AppendLog(string.Format(GetString("Log_RestoreLav"), kvp.Value));
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
                        AppendLog(string.Format(GetString("Warning_TakeOwnershipFailed"), ex.Message), true);
                    }

                    // Restore to backup state: Delete existing values first (to remove any added keys) then import
                    AppendLog(GetString("Log_CleanupPreferred"));
                    RegistryHelper.DeleteAllRegistryValues(PREFERRED_FILTERS_KEY);

                    RegistryHelper.RestoreRegistryKey(Path.GetFullPath("Preferred_default.reg"));
                    AppendLog(GetString("Log_RestorePreferred"));

                    // Restore ownership
                    try
                    {
                        RegistryHelper.ReturnOwnershipToTrustedInstaller(PREFERRED_FILTERS_KEY);
                        AppendLog(GetString("Log_RestoreOwnershipTrusted"));
                    }
                    catch (Exception ex)
                    {
                        AppendLog(string.Format(GetString("Warning_RestoreOwnershipFailed"), ex.Message), true);
                    }

                    // Cleanup backup
                    try { File.Delete("Preferred_default.reg"); } catch { }
                }

                // 3. Unregister AX
                AppendLog(GetString("Log_UnregisterAx"));

                if (File.Exists(_axPath))
                {
                    if (FilterRegistrar.UnregisterFilter(_axPath))
                        AppendLog(GetString("Log_UnregisterSuccess"));
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
                            AppendLog(GetString("Log_DeleteUser"));
                        }
                        catch (Exception ex)
                        {
                            AppendLog(string.Format(GetString("Warning_DeleteUserFailed"), ex.Message), true);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                AppendLog(string.Format(GetString("Error_Unexpected"), ex.Message + "\n" + ex.StackTrace), true);
                MessageBox.Show(string.Format(GetString("Error_Unexpected"), ex.Message), GetString("Msg_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
