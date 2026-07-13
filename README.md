# Visualizador FBX (FbxViewer)

App simples em C++ (Win32 + DirectX 11) para visualizar arquivos `.fbx` e `.obj`,
inspirado no visualizador 3D nativo do Windows.

## Recursos

- Abrir múltiplos arquivos na mesma janela (cada um em uma aba)
- Arrastar e soltar arquivos na janela
- Alternar entre visualização **com material** (cor/textura) e **sem material** (cinza neutro)
- Alternar exibição de **wireframe** (sobreposto à malha)
- Painel inferior com **vértices, edges, triângulos, número de malhas e materiais**
- Câmera orbital: arraste com botão esquerdo para orbitar, botão direito/meio para
  pan, scroll para zoom, tecla **F** para reenquadrar, tecla **W** para alternar wireframe

## Pré-requisitos

### 1. Visual Studio 2022 (Community ou superior)
Instale com o workload **"Desenvolvimento para desktop com C++"**.

### 2. Autodesk FBX SDK
1. Acesse: https://aps.autodesk.com/developer/overview/fbx-sdk
2. Baixe o **FBX SDK 2020.3.7 VS2019** (ou a versão mais recente) para Windows.
3. Instale no caminho padrão sugerido pelo instalador, algo como:
   ```
   C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7
   ```
4. Se instalar em outro caminho ou usar outra versão, edite em
   `FbxViewer.vcxproj` a linha:
   ```xml
   <FBXSDK_DIR>C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7</FBXSDK_DIR>
   ```

> Não é necessário nenhum pacote NuGet adicional — texturas (PNG/JPG/BMP)
> são carregadas via WIC, que já vem com o Windows.

## Como compilar

1. Abra `FbxViewer.sln` no Visual Studio 2022.
2. Selecione a configuração **Release** e plataforma **x64**.
3. Build > Build Solution (Ctrl+Shift+B).
4. Execute o `.exe` gerado em `x64\Release\` (a DLL do SDK e a pasta
   `shaders` são copiadas automaticamente pelo post-build).

## Solução de problemas

### Erro: `Não é possível abrir arquivo incluir: 'fbxsdk.h'`
O compilador não encontrou o FBX SDK. Verifique:
1. O SDK está realmente instalado? Abra o Windows Explorer e confirme que
   existe a pasta com uma subpasta `include` contendo `fbxsdk.h`, por exemplo:
   `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\include\fbxsdk.h`
2. O caminho em `<FBXSDK_DIR>` no `FbxViewer.vcxproj` aponta EXATAMENTE
   para essa pasta (a que contém `include`)? Atenção ao número da versão —
   se você baixou a 2020.3.4 ou 2023.x, o nome da pasta muda.
3. Depois de editar o `.vcxproj`, feche e reabra a solution no Visual Studio
   (ou clique direito no projeto > Recarregar Projeto).

### Erros `C2589 min/max` ou `UINT não declarado`
Já corrigidos nesta versão do projeto (definição `NOMINMAX` global e include
de `windows.h` no `SceneData.h`). Se aparecerem, confirme que está usando os
arquivos desta versão do zip.

### Erro de link `libfbxsdk.lib não encontrado`
A estrutura da pasta `lib` varia por versão do SDK:
- SDK 2020.x: `lib\vs2019\x64\release\`
- SDK 2023.x+: `lib\x64\release\`
O projeto já procura em ambas. Se sua versão usar outra estrutura, ajuste
`AdditionalLibraryDirectories` no `.vcxproj` para o caminho real (confira
no Explorer onde está o `libfbxsdk.lib` dentro da pasta do SDK).

## Estrutura do projeto

```
FbxViewer/
├── FbxViewer.sln
├── FbxViewer.vcxproj
├── shaders/
│   ├── MainShader.hlsl   (shading com/sem material)
│   └── WireShader.hlsl   (overlay de wireframe)
└── src/
    ├── main.cpp          (WinMain)
    ├── MainWindow.h/.cpp (janela, abas, menu, mouse/teclado)
    ├── Renderer.h/.cpp   (DirectX 11: device, swapchain, shaders, draw)
    ├── FbxLoader.h/.cpp  (importação via Autodesk FBX SDK)
    └── SceneData.h       (estruturas de dados: vértices, materiais, stats)
```

## Limitações conhecidas (mantido simples de propósito)

- Sem suporte a animação/esqueleto (apenas geometria estática).
- Texturas: PNG/JPG/BMP/TIFF via WIC. DDS/TGA não são suportados.
- Uma luz direcional fixa; sem sombras.
- A contagem de "vértices" reflete os vértices de GPU (duplicados nas costuras
  de UV/normal), igual à maioria dos viewers — já a contagem de "edges" é da
  topologia original (arestas únicas por malha).
