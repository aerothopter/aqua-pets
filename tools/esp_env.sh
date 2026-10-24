# esp_env.sh - ESP-IDF for the flash tools; SOURCED by them, not run.
#
# A Mac (or Linux): ~/esp/esp-idf/export.sh, as always. Windows (Git Bash,
# 2026-10-23): ESP-IDF's install is a PowerShell profile whose idf.py and
# espsecure.py are an alias and a function no bash can call. Start the tool
# from the ESP-IDF 5.4.4 PowerShell (its profile sets IDF_PATH and PATH,
# which bash inherits) and they become bash functions over its python:
#
#   PS> . C:\Espressif\tools\Microsoft.v5.4.4.PowerShell_profile.ps1
#   PS> & "C:\Program Files\Git\bin\bash.exe" tools/flash.sh
#
# Returns 1 when neither is there (the caller says so and stops).
if [ -f ~/esp/esp-idf/export.sh ]; then
  . ~/esp/esp-idf/export.sh > /dev/null 2>&1 || return 1
elif [ -n "${IDF_PATH:-}" ] && command -v python > /dev/null; then
  # idf.py does nothing at all while MSYSTEM is set (it warns "MSys/Mingw is no
  # longer supported" and never calls main), and Git Bash puts MSYSTEM back
  # into every Windows program it starts - so python drops it, then runs idf.py
  idf.py() {
    python -c 'import os, sys, runpy; os.environ.pop("MSYSTEM", None); sys.argv = sys.argv[1:]; sys.path.insert(0, os.path.dirname(sys.argv[0])); runpy.run_path(sys.argv[0], run_name="__main__")' \
      "$IDF_PATH/tools/idf.py" "$@"
  }
  espsecure.py() { python -m espsecure "$@"; }
else
  return 1
fi
