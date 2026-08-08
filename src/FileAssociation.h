#pragma once
#include <windows.h>

// Registro do aplicativo como visualizador de arquivos 3D. Tudo em HKCU, sem
// precisar de privilegio de administrador e sem o .bat externo.
namespace fileassoc
{
    // Chamado na inicializacao: registra se ainda nao foi registrado ou se o
    // executavel mudou de lugar. Nos demais casos nao escreve nada.
    void EnsureRegistered();

    // Forca o registro (item de menu). Retorna false se algo falhou.
    bool RegisterNow();

    // Abre a pagina "Aplicativos padrao" do Windows, onde o usuario escolhe
    // qual app abre cada extensao. Definir o padrao por conta propria nao e
    // permitido pelo Windows 10/11 — a escolha e sempre do usuario.
    void OpenDefaultAppsSettings();
}
