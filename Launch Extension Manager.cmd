@echo off
cd /d "%~dp0"
python Halo_Extension_Manager.pyw
if errorlevel 1 pause
