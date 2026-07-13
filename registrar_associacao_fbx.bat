@echo off
setlocal
chcp 65001 > nul

REM ============================================================
REM  Registra o FbxViewer como aplicativo para arquivos .fbx
REM  (registro por usuario, HKCU - nao precisa de administrador)
REM
REM  >>> EDITE A LINHA ABAIXO com o caminho real do seu exe <<<
REM ============================================================
set "EXEPATH=F:\FBX Viewer Files\FbxViewer\x64\Release\FbxViewer.exe"

if not exist "%EXEPATH%" (
    echo.
    echo [ERRO] Executavel nao encontrado em:
    echo   %EXEPATH%
    echo.
    echo Edite este .bat e corrija a linha "set EXEPATH=" com o caminho
    echo correto do FbxViewer.exe, depois execute novamente.
    echo.
    pause
    exit /b 1
)

echo Registrando o FbxViewer para arquivos .fbx...

REM ProgID do aplicativo (a "identidade" do tipo de arquivo)
reg add "HKCU\Software\Classes\FbxViewer.Model" /ve /d "Modelo 3D FBX" /f > nul
reg add "HKCU\Software\Classes\FbxViewer.Model\DefaultIcon" /ve /d "\"%EXEPATH%\",0" /f > nul
reg add "HKCU\Software\Classes\FbxViewer.Model\shell\open\command" /ve /d "\"%EXEPATH%\" \"%%1\"" /f > nul

REM Associa a extensao .fbx ao ProgID e adiciona ao menu "Abrir com"
reg add "HKCU\Software\Classes\.fbx" /ve /d "FbxViewer.Model" /f > nul
reg add "HKCU\Software\Classes\.fbx\OpenWithProgids" /v "FbxViewer.Model" /t REG_NONE /d "" /f > nul

REM Registra o app em "Applications" (necessario p/ o seletor do Windows)
reg add "HKCU\Software\Classes\Applications\FbxViewer.exe\shell\open\command" /ve /d "\"%EXEPATH%\" \"%%1\"" /f > nul
reg add "HKCU\Software\Classes\Applications\FbxViewer.exe" /v "FriendlyAppName" /d "Visualizador FBX" /f > nul

REM Remove a escolha anterior do usuario para .fbx, se existir - o Windows
REM protege essa chave e ela tem prioridade sobre tudo; ao remover, a nossa
REM associacao acima passa a valer (o Windows pode confirmar 1x no 1o clique)
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.fbx\UserChoice" /f > nul 2>&1

REM Reinicia o Explorer para aplicar imediatamente
echo Reiniciando o Windows Explorer para aplicar...
taskkill /f /im explorer.exe > nul 2>&1
start explorer.exe

echo.
echo Pronto! De duplo clique em um arquivo .fbx para testar.
echo Se o Windows perguntar com qual app abrir, escolha
echo "Visualizador FBX" e marque "Sempre".
echo.
pause
