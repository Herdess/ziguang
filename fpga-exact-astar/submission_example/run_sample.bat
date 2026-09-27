@echo off
setlocal
cd /d "%~dp0"
estimate.exe -in delay_estimate_request.csv -out delay_estimate_result.csv
if errorlevel 1 exit /b %errorlevel%
type delay_estimate_result.csv
