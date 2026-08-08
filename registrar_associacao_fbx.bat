@echo off
setlocal
chcp 65001 > nul

REM ============================================================
REM  NAO E MAIS NECESSARIO RODAR ESTE ARQUIVO.
REM
REM  A partir da versao 1.1 o proprio FbxViewer.exe se registra
REM  sozinho na primeira vez que e aberto (e sempre que o .exe
REM  muda de pasta). Basta abrir o programa uma vez.
REM
REM  Este .bat continua aqui apenas como atalho para dois casos:
REM    1) forcar o registro sem abrir a janela do programa;
REM    2) abrir a tela de "Aplicativos padrao" do Windows, onde
REM       voce escolhe qual programa abre cada extensao.
REM
REM  Diferente da versao antiga, ele nao mexe na chave UserChoice
REM  nem reinicia o Windows Explorer: definir o programa PADRAO e
REM  uma escolha que o Windows reserva ao usuario.
REM ============================================================

REM Procura o executavel: primeiro ao lado deste .bat, depois nas
REM pastas de build tipicas do projeto.
set "EXEPATH=%~dp0FbxViewer.exe"
if not exist "%EXEPATH%" set "EXEPATH=%~dp0x64\Release\FbxViewer.exe"
if not exist "%EXEPATH%" set "EXEPATH=%~dp0x64\Debug\FbxViewer.exe"

if not exist "%EXEPATH%" (
    echo.
    echo [ERRO] Nao encontrei o FbxViewer.exe a partir de:
    echo   %~dp0
    echo.
    echo Compile o projeto ^(Release ^| x64^) ou copie este .bat para
    echo a mesma pasta do FbxViewer.exe e execute de novo.
    echo.
    pause
    exit /b 1
)

echo Registrando o Visualizador 3D para arquivos
echo .fbx .obj .ply .glb .gltf .dae .3ds .dxf
echo.

REM Abrir o programa uma vez ja grava todo o registro (HKCU).
start "" "%EXEPATH%"

echo Pronto.
echo.
echo Para deixar o Visualizador 3D como programa PADRAO de alguma
echo extensao, use a tela que vai abrir a seguir (ou o menu
echo Ferramentas ^> Aplicativos padrao do Windows, dentro do app):
echo procure por "Visualizador 3D" e escolha as extensoes.
echo.
pause

start "" ms-settings:defaultapps
