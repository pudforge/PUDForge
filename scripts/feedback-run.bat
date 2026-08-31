@echo off
rem Work the feedback queue: read what is accepted, fix it, ship it, close it.
rem
rem Run it by hand, from anywhere. It starts Claude in the repo with /feedback
rem as the opening prompt, and then it is an ordinary session - permissions are
rem asked for as usual and you can watch, answer and stop it.
rem
rem It is deliberately not unattended yet. Scheduling this means letting it
rem build, push and publish a release with nobody at the keyboard, and that is
rem worth turning on once the loop has been watched a few times rather than on
rem the first run. When that day comes the change is --permission-mode and a
rem Task Scheduler entry; see the note at the bottom.

setlocal

rem The repo, wherever this file is: scripts\ is one below the root.
set "REPO=%~dp0.."
pushd "%REPO%" || (echo Could not enter %REPO% & exit /b 1)

where claude >nul 2>&1 || (
  echo Claude Code is not on PATH. Install it, or open a shell that has it.
  popd & exit /b 1
)

echo Working the PUDForge feedback queue in %CD%
echo.

rem Opus, because this ends in judgement calls: whether a report reproduces,
rem whether the corpus agrees with it, and whether it is a decision for Kalle
rem rather than a fix.
claude --model opus "/feedback"
set "RESULT=%ERRORLEVEL%"

popd
exit /b %RESULT%

rem ---------------------------------------------------------------------------
rem To schedule it later, unattended, the line becomes something like:
rem
rem   claude --model opus --permission-mode bypassPermissions -p "/feedback"
rem
rem -p prints and exits rather than waiting for a person, and the permission
rem mode stops it asking one. Both are needed together and neither is safe on
rem its own terms: that command may push commits and publish a release without
rem anybody seeing it first. The gate that makes it tolerable is the `accepted`
rem label - nothing is worked until a person has agreed to it - so keep that
rem gate honest before turning this on.
