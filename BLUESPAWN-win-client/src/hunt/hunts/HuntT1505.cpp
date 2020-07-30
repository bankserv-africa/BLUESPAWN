#include "hunt/hunts/HuntT1505.h"

#include "util/StringUtils.h"
#include "util/filesystem/FileSystem.h"
#include "util/log/Log.h"

#include "scan/YaraScanner.h"
#include "user/bluespawn.h"

namespace Hunts {
    HuntT1505::HuntT1505() : Hunt(L"T1505 - Server Software Component") {
        dwCategoriesAffected = (DWORD) Category::Files;
        dwSourcesInvolved = (DWORD) DataSource::FileSystem;
        dwTacticsUsed = (DWORD) Tactic::Persistence | (DWORD) Tactic::PrivilegeEscalation;
    }

    std::vector<std::shared_ptr<Detection>> HuntT1505::RunHunt(const Scope& scope) {
        HUNT_INIT();

        // Looks for T1505.003: Web Shell
        //PHP regex credit to: https://github.com/emposha/PHP-Shell-Detector
        php_vuln_functions.assign(
            R"(preg_replace.*\/e|`.*?\$.*?`|\bcreate_function\b|\bpassthru\b|\bshell_exec\b|\bexec\b|\bbase64_decode\b|\bedoced_46esab\b|\beval\b|\bsystem\b|\bproc_open\b|\bpopen\b|\bcurl_exec\b|\bcurl_multi_exec\b|\bparse_ini_file\b|\bshow_source\b)");
        asp_indicators.assign(
            R"(\bcmd.exe\b|\bpowershell.exe\b|\bwscript.shell\b|\bprocessstartinfo\b|\bcreatenowindow\b|\bcmd\b|\beval request\b|\bexecute request\b|\boscriptnet\b|createobject\("scripting.filesystemobject"\))");
        jsp_indicators.assign(R"(\bcmd.exe\b|\bpowershell.exe\b|\bgetruntime\(\)\.exec\b)");

        for(std::wstring path : web_directories) {
            auto hWebRoot = FileSystem::Folder(path);
            FileSystem::FileSearchAttribs attribs;
            attribs.extensions = web_exts;
            std::vector<FileSystem::File> files = hWebRoot.GetFiles(attribs, -1);

            for(const auto& file : files) {
                long offset = 0;
                unsigned long targetAmount = 1000000;
                DWORD amountRead = 0;
                auto file_ext = ToLowerCaseW(file.GetFileAttribs().extension);
                bool detected = false;

                do {
                    auto read = file.Read(targetAmount, offset, &amountRead);
                    read.SetByte(amountRead, '\0');
                    std::string sus_file = ToLowerCaseA(*read.ReadString());
                    if(file_ext.compare(L".php") == 0) {
                        if(regex_search(sus_file, match_index, php_vuln_functions)) {
                            CREATE_DETECTION_WITH_CONTEXT(Certainty::Strong, FileDetectionData{ file },
                                                          DetectionContext{ ADD_SUBTECHNIQUE_CONTEXT(t1505_003) });
                            LOG_INFO(1, L"Located likely web shell in file "
                                            << file.GetFilePath() << L" in text "
                                            << StringToWidestring(
                                                   sus_file.substr(match_index.position(), match_index.length())));
                            detected = true;
                            break;
                        }
                    } else if(file_ext.substr(0, 4).compare(L".jsp") == 0) {
                        if(regex_search(sus_file, match_index, jsp_indicators)) {
                            CREATE_DETECTION_WITH_CONTEXT(Certainty::Strong, FileDetectionData{ file },
                                                          DetectionContext{ ADD_SUBTECHNIQUE_CONTEXT(t1505_003) });
                            LOG_INFO(1, L"Located likely web shell in file "
                                            << file.GetFilePath() << L" in text "
                                            << StringToWidestring(
                                                   sus_file.substr(match_index.position(), match_index.length())));
                            detected = true;
                            break;
                        }
                    } else if(file_ext.substr(0, 3).compare(L".as") == 0) {
                        if(regex_search(sus_file, match_index, asp_indicators)) {
                            CREATE_DETECTION_WITH_CONTEXT(Certainty::Strong, FileDetectionData{ file },
                                                          DetectionContext{ ADD_SUBTECHNIQUE_CONTEXT(t1505_003) });
                            LOG_INFO(1, L"Located likely web shell in file "
                                            << file.GetFilePath() << L" in text "
                                            << StringToWidestring(
                                                   sus_file.substr(match_index.position(), match_index.length())));
                            detected = true;
                            break;
                        }
                    }
                    offset += amountRead - 1000;
                } while(static_cast<SIZE_T>(offset + 1000) < file.GetFileSize());

                // Use YARA to also scan the files if our regex didn't detect anything suspicious
                if(!detected) {
                    const auto& yara = YaraScanner::GetInstance();
                    YaraScanResult result{ yara.ScanFile(file) };
                    if(!result && result.vKnownBadRules.size() > 0) {
                        CREATE_DETECTION_WITH_CONTEXT(Certainty::Strong, FileDetectionData{ file, result },
                                                      DetectionContext{ ADD_SUBTECHNIQUE_CONTEXT(t1505_003) });
                    }
                }
            }
        }

        HUNT_END();
    }

    std::vector<std::unique_ptr<Event>> HuntT1505::GetMonitoringEvents() {
        std::vector<std::unique_ptr<Event>> events;

        // Looks for T1505.003: Web Shell
        for(auto dir : web_directories) {
            auto folder = FileSystem::Folder{ dir };
            if(folder.GetFolderExists()) {
                events.push_back(std::make_unique<FileEvent>(folder));
                for(auto subdir : folder.GetSubdirectories()) {
                    events.push_back(std::make_unique<FileEvent>(subdir));
                }
            }
        }

        return events;
    }
}   // namespace Hunts