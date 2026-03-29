@echo off
setlocal
set "SCRIPT_DIR=%~dp0"

if "%~1"=="" (
  set /p "TARGET=Drag an EPUB file or books folder here, then press Enter: "
  set "TARGET=%TARGET:"=%"
  where py >nul 2>nul
  if not errorlevel 1 (
    py -3 "%SCRIPT_DIR%generate_epub_thumbnails.py" "%TARGET%"
  ) else (
    python "%SCRIPT_DIR%generate_epub_thumbnails.py" "%TARGET%"
  )
) else (
  where py >nul 2>nul
  if not errorlevel 1 (
    py -3 "%SCRIPT_DIR%generate_epub_thumbnails.py" %*
  ) else (
    python "%SCRIPT_DIR%generate_epub_thumbnails.py" %*
  )
)

echo.
pause
