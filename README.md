# Visualizador 3D (FbxViewer)

App em C++ (Win32 + DirectX 11) para visualizar modelos 3D, inspirado no
visualizador 3D nativo do Windows.

## Formatos suportados

| Extensão | Como é lido |
| --- | --- |
| `.fbx` | Autodesk FBX SDK |
| `.obj` | Autodesk FBX SDK |
| `.dae`, `.3ds`, `.dxf` | Autodesk FBX SDK |
| `.ply` | leitor próprio (ascii, `binary_little_endian` e `binary_big_endian`) |
| `.glb`, `.gltf` | leitor próprio de glTF 2.0 (buffers e texturas embutidos, em data URI ou externos) |

## Recursos

- Abrir vários arquivos na mesma janela, cada um em uma aba
  - **Ctrl+Tab** / **Ctrl+Shift+Tab** (ou Ctrl+PageDown / Ctrl+PageUp) alternam entre as abas
  - **Arquivo > Abrir recentes** guarda os últimos 12 arquivos abertos
  - **Arquivo > Abrir em nova janela** (Ctrl+N) abre uma segunda janela independente
- Arrastar e soltar arquivos na janela
- Duas visões por arquivo, na faixa de abas sobre o viewport:
  - **Modelo 3D** — a cena renderizada
  - **Mapa UV e textura** — o desenho das UVs sobre a imagem de textura, com
    zoom (roda do mouse), pan (arrastar) e seletor de material
- **Gizmo de orientação** no canto do viewport, no estilo do Blender: clicar em
  uma das esferas alinha a câmera àquele eixo
- **Grade** opcional no plano do chão, com linhas de eixo coloridas e tom que
  se adapta ao fundo (tecla **G**)
- **Exportar imagem** (Ctrl+E): PNG/JPEG/BMP, presets de resolução ou tamanho
  livre, fundo transparente, com ou sem sombras e grade, e cópia direta para a
  área de transferência
- Alternar entre visualização **com material** (cor/textura/cor por vértice) e
  **sem material** (cinza neutro)
- Alternar exibição de **wireframe** e de **sombras** (com penumbra suave)
- Painel lateral **rolável**, com **vértices, edges, triângulos, malhas,
  materiais e draw calls**
- **Ambiente e iluminação** totalmente editáveis, como no visualizador nativo:
  seis presets como ponto de partida e, para cada fonte — luz principal, três
  auxiliares, luz ambiente, plano de fundo e cor do chão — cor em HSV ou RGB,
  campo hexadecimal, seletor de cor do Windows e intensidade
- Câmera orbital: arraste com o botão esquerdo para orbitar, botão direito/meio
  para pan, roda para zoom, **F** para reenquadrar, **W** para alternar wireframe
- A janela reabre com a **posição e o tamanho da última sessão** (inclusive o
  estado maximizado)
- O app se **registra sozinho** como opção de visualizador para os formatos
  acima, na primeira execução

## Associação de arquivos

Na primeira vez que o programa é aberto, ele grava em `HKEY_CURRENT_USER` o
necessário para aparecer em **Abrir com** para todas as extensões suportadas.
Não é preciso privilégio de administrador nem rodar nenhum `.bat` — e nas
aberturas seguintes nada é gravado no registro (só quando o `.exe` muda de
pasta).

Definir o programa **padrão** de uma extensão continua sendo escolha do
usuário: o Windows 10/11 não permite que um aplicativo faça isso por conta
própria. Use **Ferramentas > Abrir Aplicativos padrão do Windows** e procure
por "Visualizador 3D".

O arquivo `registrar_associacao_fbx.bat` foi mantido apenas como atalho para
essa tela; ele não é mais necessário.

## Pré-requisitos

### 1. Visual Studio 2022 (Community ou superior)
Instale com o workload **"Desenvolvimento para desktop com C++"**.

### 2. Autodesk FBX SDK
1. Acesse: https://aps.autodesk.com/developer/overview/fbx-sdk
2. Baixe o **FBX SDK 2020.3.x VS2019** (ou a versão mais recente) para Windows.
3. Instale no caminho padrão sugerido pelo instalador, algo como:
   ```
   C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.9
   ```
4. Se instalar em outro caminho ou usar outra versão, edite em
   `FbxViewer.vcxproj` a linha:
   ```xml
   <FBXSDK_DIR>C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.9</FBXSDK_DIR>
   ```

> Não é necessário nenhum pacote NuGet adicional. As texturas (PNG/JPG/BMP/TIFF)
> são carregadas via WIC, que já vem com o Windows, e os leitores de PLY e glTF
> são próprios — sem dependências externas.

## Como compilar

1. Abra `FbxViewer.sln` no Visual Studio 2022.
2. Selecione a configuração **Release** e plataforma **x64**.
3. Build > Build Solution (Ctrl+Shift+B).
4. Execute o `.exe` gerado em `x64\Release\` (a DLL do SDK e a pasta
   `shaders` são copiadas automaticamente pelo post-build).

## Solução de problemas

### Erro: `Não é possível abrir arquivo incluir: 'fbxsdk.h'`
O compilador não encontrou o FBX SDK. Verifique:
1. O SDK está realmente instalado? Confirme que existe a pasta com uma subpasta
   `include` contendo `fbxsdk.h`, por exemplo:
   `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.9\include\fbxsdk.h`
2. O caminho em `<FBXSDK_DIR>` no `FbxViewer.vcxproj` aponta EXATAMENTE
   para essa pasta (a que contém `include`)? Atenção ao número da versão.
3. Depois de editar o `.vcxproj`, feche e reabra a solution no Visual Studio
   (ou clique direito no projeto > Recarregar Projeto).

### Erro de link `libfbxsdk.lib não encontrado`
A estrutura da pasta `lib` varia por versão do SDK:
- SDK 2020.x: `lib\vs2019\x64\release\`
- SDK 2023.x+: `lib\x64\release\`

O projeto já procura em ambas. Se sua versão usar outra estrutura, ajuste
`AdditionalLibraryDirectories` no `.vcxproj` para o caminho real.

### Acentos aparecem trocados na interface
Os fontes estão em UTF-8 sem BOM e o projeto compila com `/utf-8`. Se você
remover essa opção do `.vcxproj`, o MSVC volta a ler os arquivos na code page
do sistema e os acentos quebram.

## Estrutura do projeto

```
FbxViewer/
├── FbxViewer.sln
├── FbxViewer.vcxproj
├── FbxViewer.rc            (ícone do executável + informações de versão)
├── res/app.ico
├── shaders/
│   ├── MainShader.hlsl     (shading com/sem material, sombras, chão)
│   ├── WireShader.hlsl     (overlay de wireframe)
│   └── Overlay.hlsl        (gizmo de orientação e aba de UV)
└── src/
    ├── main.cpp            (WinMain, instância única, --new-window)
    ├── MainWindow.h/.cpp   (janela, abas, menu, sidebar, mouse/teclado)
    ├── ExportImageDialog.h/.cpp (caixa "Exportar imagem" + WIC + clipboard)
    ├── Renderer.h/.cpp     (DirectX 11: device, swapchain, shaders, draw)
    ├── ModelLoader.h/.cpp  (despacha o arquivo para o leitor certo)
    ├── FbxLoader.h/.cpp    (FBX, OBJ, DAE, 3DS, DXF via FBX SDK)
    ├── PlyLoader.h/.cpp    (Stanford PLY)
    ├── GltfLoader.h/.cpp   (glTF 2.0 / GLB)
    ├── Json.h/.cpp         (parser JSON mínimo usado pelo glTF)
    ├── LoaderUtils.h       (helpers comuns aos leitores)
    ├── AppSettings.h/.cpp  (posição/tamanho da janela no registro)
    ├── FileAssociation.h/.cpp (registro como visualizador de arquivos 3D)
    └── SceneData.h         (estruturas de dados: vértices, materiais, stats)
```

## Notas de implementação

### Sistema de coordenadas
Todos os leitores normalizam a cena para **right-handed, Y para cima** (a
convenção de OBJ, PLY e glTF) e só então espelham o eixo X para o sistema
**left-handed** usado pelas matrizes do DirectX. Como espelhar inverte o
sentido dos triângulos, cada leitor também inverte a ordem dos índices — sem
isso a imagem sai espelhada (textos ficam ao contrário) e o backface culling
descarta o lado errado da malha.

## Limitações conhecidas

- Sem suporte a animação/esqueleto (apenas geometria estática).
- Texturas: PNG/JPG/BMP/TIFF via WIC. DDS/TGA não são suportados.
- Do glTF é lido o `baseColorTexture`/`baseColorFactor`; mapas de normal,
  metallic-roughness e emissivo são ignorados.
- PLY sem faces (nuvem de pontos) não é exibido.
- Uma luz direcional principal + até 3 auxiliares; uma única sombra projetada.
  As auxiliares têm direção fixa (esquerda, direita e contorno) — só cor,
  intensidade e liga/desliga são editáveis.
- A contagem de "vértices" reflete os vértices de GPU (duplicados nas costuras
  de UV/normal), igual à maioria dos viewers — já a contagem de "edges" é da
  topologia original (arestas únicas por malha).
- Os ajustes de iluminação valem para a sessão: ainda não são salvos entre
  execuções (só o tamanho da janela e a lista de recentes são).
- A exportação de imagem parte da aba "Modelo 3D"; o mapa de UV não é
  exportável.
