# Patch FFmpeg `BGRA1010102`/`RGBA1010102` — registro de decisões

Este documento registra tudo o que motivou o estado atual do patch aplicado pelo
`build-deps` ao FFmpeg vendorizado, incluindo cada achado (review upstream, OCR e
auditorias internas), a solução escolhida e o porquê de cada escolha.

## 1. Objetivo

O HDR 10-bit no Linux (KMS e PipeWire/portal) usa os DRM fourccs
`DRM_FORMAT_BGRA1010102` e `DRM_FORMAT_RGBA1010102` — RGB 10:10:10 com 2 bits de
alpha nos bits baixos. O FFmpeg upstream não define esses formatos, então o
`build-deps` aplica um patch local que os registra em `libavutil` e implementa os
handlers no `libswscale`, permitindo captura HDR 10-bit → conversão → encode.

- PR upstream: `LizardByte/build-deps#762` (review por ReenigneArcher).
- Branch preparada no fork: `luanweslley77/build-deps:fix/ffmpeg-pixfmt-review`.
- Base: `upstream/master` (`502dbc5`), FFmpeg vendorizado **9.0.2** (`946fcce`).

## 2. Dimensões do formato

| Formato                    | Layout na memória                                                   |
| -------------------------- | ------------------------------------------------------------------- |
| `AV_PIX_FMT_BGRA1010102LE` | B(31:22) G(21:12) R(11:2) A(1:0), little-endian                     |
| `AV_PIX_FMT_RGBA1010102LE` | R(31:22) G(21:12) B(11:2) A(1:0), little-endian                     |
| `..._BE`                   | mesmos campos numéricos, armazenados big-endian                     |
| `AV_PIX_FMT_BGRA1010102`   | alias `AV_PIX_FMT_NE(BGRA1010102BE, BGRA1010102LE)` (endian nativo) |

## 3. Achados e soluções

### 3.1 [Review #762 — P1] IDs de `AVPixelFormat` deslocados

**Achado.** Os quatro membros novos foram inseridos antes de `AV_PIX_FMT_P210BE`,
deslocando o valor numérico de todos os formatos seguintes. Valores de
`AVPixelFormat` atravessam a fronteira da API: bibliotecas compiladas contra
cabeçalhos antigos passariam a interpretar IDs errados (quebra de ABI).

**Solução.** Mover os quatro para imediatamente antes de `AV_PIX_FMT_NB`
(fim do enum). Descritores em `pixdesc.c` e macros `AV_PIX_FMT_NE` foram movidos
para o fim das respectivas listas, mantendo a ordem enum ↔ tabela.

**Porquê.** É a única posição que garante estabilidade dos IDs existentes sem
"buraco" no enum; segue a convenção do FFmpeg para formatos novos.

### 3.2 [Review #762 — P1] `{ 1, 1 }` anunciava saída sem writers

**Achado.** `legacy_format_entries` marcava entrada **e** saída como suportadas,
mas `libswscale/output.c` não tinha writers; `sws_getContext()` aceitaria o
formato como destino e produziria lixo (ou cairia em caminho inexistente).

**Opções.** (a) marcar `{ 1, 0 }` (só entrada); (b) implementar os writers.

**Solução escolhida: (b).** Implementados:

- caminho LUT/packed: `yuv2rgb.c` (tabelas 10-bit próprias) + `yuv2rgb_write()`;
- caminho full chroma (`SWS_FULL_CHR_H_INT`): `yuv2rgb_write_full()`;
- wrappers `YUV2RGBWRAPPER` (variantes com alpha e com alpha opaco) e dispatch em
  `ff_sws_init_output_funcs()`.

Alpha opaco `3` quando a fonte não tem alpha; alpha real quando tem.

**Porquê.** Mantém paridade com `X2RGB10/X2BGR10` (que têm writers completos) e o
formato passa a ser destino válido de verdade; `{1,0}` reduziria capacidade.

### 3.3 [Review #762 — P2] Alpha sem leitor (`alpToYV12`)

**Achado.** Os descritores marcam `AV_PIX_FMT_FLAG_ALPHA`, mas
`ff_sws_init_input_funcs()` não tinha `alpToYV12` para os formatos. Em conversão
para destino com alpha (`needAlpha`), a linha de alpha ficava sem preencher.

**Solução.** `packed1010102leToA_c` lê os 2 bits
(`AV_RL32(_src + i * 4) & 3`) e expande para a escala interna de **14 bits**
(`* 0x1555`, replicação de bits), a mesma usada por `rgbaToA_c`, e é ligado no
switch de `c->needAlpha` para LE.

**Porquê 0x1555.** O pipeline legado usa alpha interno de 14 bits para fontes
RGB de 8 bits; a replicação de bits mantém proporção exata e o round-trip 2 bits
→ 2 bits idêntico. Testes: `TwoBitAlphaExpandsToEightBit` e
`TwoBitRoundTripPreservesAlpha`.

### 3.4 [OCR] Writers full inalcançáveis

**Achado.** `sws_init_context()` remove `SWS_FULL_CHR_H_INT` para destinos RGB
fora de uma lista de exceções (`libswscale/utils.c`), que inclui
`X2RGB10LE/X2BGR10LE`. Sem entrar na lista, os writers `_full_` eram código
morto e o usuário recebia o warning "full chroma interpolation ... not yet
implemented".

**Solução.** Adicionar `BGRA1010102LE`/`RGBA1010102LE` à lista de exceções
(mesmo lugar do X2). O warning sumiu e o caminho full passou a ser exercitado
de verdade pelos testes (que passaram a cobrir `SWS_FULL_CHR_H_INT`).

### 3.5 [OCR] Nome do leitor de alpha

**Achado.** `bgra1010102leToA_c` servia BGRA e RGBA; o prefixo sugeria
especificidade que não existe (só lê os 2 bits baixos).

**Solução.** Renomeado para `packed1010102leToA_c` (neutro).

### 3.6 [Observação interna] Cast para `uint32_t *` no leitor

**Achado.** O leitor fazia `const uint32_t *src = (const uint32_t *)_src;` —
contrato de memória implícito para um buffer de bytes.

**Solução.** `AV_RL32(_src + i * 4) & 3`, sem cast; o `AV_RL32` já resolve
leitura de 4 bytes e endianness.

### 3.7 [Análise] Inconsistência LE/BE

**Achado.** O patch expunha quatro formatos (LE/BE), mas o `libswscale`
implementava só LE; havia mistura de alias nativo (`AV_PIX_FMT_BGRA1010102`) com
IDs LE explícitos e writers `AV_WL32`.

**Investigação (medida, não suposta).**

- Matriz de suporte idêntica ao `X2RGB10/X2BGR10`: legacy/stable = **LE in/out=1,
  BE=0**; o BE sempre foi rejeitado (`sws_isSupported*`).
- O backend ops (instável) funcionava para `X2RGB10BE` (usa conversão de 3
  componentes, `u32_to_f32_yzw`), mas para nossos formatos o decode BE exigia
  `u32_to_f32_xyzw` (4 componentes, incluindo alpha), **sem implementação**.
- O arquivo `uops_macros.h` é auto-gerado a partir de todos os formatos;
  regenerá-lo com o patch resultou **byte a byte idêntico** — o gerador não
  enumera BE, então a uop nunca seria emitida. Implementar BE de verdade exigiria
  mudar o core de ops (fora do escopo de um patch de formato).

**Solução escolhida: (b) — manter os 4 formatos públicos, swscale LE-only
explícito.**

- `format.c`: mantidas só as entradas legacy LE; removidas as entradas BE de
  `fmt_info_irregular`.
- `output.c`: writer packed e wrappers passaram a usar IDs **LE explícitos**
  (não mais o alias nativo).
- `utils.c`: exceção full-chroma só LE.
- Descritores/enums BE permanecem em `libavutil` (paridade de API com os demais
  formatos packed e convenção `AV_PIX_FMT_NE`).
- Teste negativo no Sunshine (`BigEndianVariantsUnsupported`) fixa o contrato:
  descritores existem, mas `sws_isSupported*`/`sws_test_format` = 0.

**Porquê não BE completo.** Exigiria, no mínimo, uma uop nova no backend ops e
mudanças no gerador; nenhum consumidor (Sunshine/KMS/PipeWire) usa BE. Declarar
suporte pela metade era pior.

### 3.8 [OCR 128k] Bug em host big-endian no writer packed

**Achado.** No host BE, as tabelas do caminho packed eram byte-swapped
(`isNotNe`), e o alpha runtime era somado nos bits 1:0; com a tabela trocada ele
deveria ir em bits 25:24. Resultado: alpha errado e campo B perturbado.

**Solução escolhida.** Em vez do shift condicional sugerido (`<< 24` em BE):

- Removido o byte-swap das tabelas no bloco LUT de `yuv2rgb.c` (e as entradas de
  `isNotNe` para os formatos).
- O writer packed passou a escrever com `AV_WL32()`, exatamente como o writer
  full já fazia — o store resolve a ordem de bytes de forma explícita e o alpha
  fica correto em qualquer host.

**Porquê.** Elimina a classe do problema (não depende de "onde o byte-swap
deixou o campo"), remove a mistura `NE`/nativo do caminho e casa com o idioma já
usado no writer full.

### 3.9 [Auditoria final] Entradas ops para LE também não compilavam

**Achado.** Mesmo forçando `SWS_UNSTABLE`, a cadeia ops do formato LE falhava no
compile (etapa de dither sem implementação no backend) e só funcionava pelo
fallback silencioso para o legacy. Ou seja, a entrada LE em `fmt_info_irregular`
também anunciava suporte ops que não existia.

**Solução.** Remover as entradas de `fmt_info_irregular` por completo. O
`libswscale` tem **uma única implementação honesta**: o backend legacy (LE).
Backends não-legacy rejeitam com `ENOTSUP` limpo (sem tentativa falha, sem
warning) e o fallback para legacy continua funcionando quando `SWS_UNSTABLE` é
pedido.

## 4. Modelo final de suporte

| Consulta                               | `...1010102LE`              | `...1010102BE` |
| -------------------------------------- | --------------------------- | -------------- |
| `sws_isSupportedInput/Output` (stable) | 1                           | 0              |
| `sws_test_format` (stable)             | 1                           | 0              |
| backend legacy (`sws_getContext`)      | sim                         | rejeitado      |
| backend ops (`SWS_UNSTABLE`/frame API) | rejeitado (fallback legacy) | rejeitado      |
| `av_pix_fmt_desc_get` (libavutil)      | existe                      | existe         |

## 5. Higiene de commits

Antes (7 commits iterativos, com commits revisando commits anteriores):

```
86a54cd fix(ffmpeg): drop the unused ops backend entries for 1010102
dd9fe23 fix(ffmpeg): write 1010102 output pixels with explicit LE stores
533b545 fix(ffmpeg): scope 1010102 swscale support to little-endian
18840ad refactor(ffmpeg): read packed 1010102 alpha from the byte buffer
e8c5e48 fix(ffmpeg): make 1010102 full-chroma writers reachable
2007004 build(ffmpeg): wire libavutil patches into the shared patch list
e7d143f feat(ffmpeg): add BGRA1010102/RGBA1010102 pixel formats for 10-bit HDR
```

Depois (2 commits, árvore idêntica ao estado final):

```
5c6bf7b build(ffmpeg): wire libavutil patches into the shared patch list
192fd4a feat(ffmpeg): add BGRA1010102/RGBA1010102 pixel formats for 10-bit HDR
```

O commit `feat` contém: `CMakeLists.txt` (+1, opção
`BUILD_FFMPEG_LIBAVUTIL_PATCHES`) e o patch (`411` linhas, 7 arquivos FFmpeg).
O commit `build` contém `cmake/ffmpeg/ffmpeg.cmake` (lista única
`FFMPEG_PATCH_FILES`). Nenhum commit desfaz trabalho de outro.

## 6. Verificação

- Patch aplica limpo (`git apply --check`) no FFmpeg 9.0.2.
- Build real: `cmake --build cmake-build-deps --target ffmpeg` + `cmake --install`
  (dist atualizado).
- Teste C standalone: round-trip alpha 1:1, mapa 2→8 bits
  (`0x00/0x55/0xAB/0xFF`), alpha opaco = 3, ordem de canais, matriz de suporte.
- Sunshine `test_sunshine`: **16/16** (`HdrAlpha` 7, `HdrE2E` 2,
  `PipeWireFormatTest` 5, `E2EPipeWireVideo` 2), incluindo o teste negativo de BE.
- `git diff --check` limpo; sem tabs; linhas longas apenas nos comentários de
  enum no estilo do `X2RGB10`.

## 7. Apêndice — teto de tokens do OCR

Durante o processo, o OCR falhava os arquivos grandes do `libswscale` com
`main_task did not complete before stopping (round 1/2)`. Diagnóstico:

- As respostas do modelo terminavam com `completion_tokens == 16384`, `content`
  vazio e sem tool call (raciocínio cortado).
- O provedor aceita mais (um teste direto sem `max_tokens` devolveu 90.100
  tokens; com 32.000, 32.000).
- Captura do corpo real via proxy local (`uv run python`) mostrou que o **OCR**
  enviava `"max_completion_tokens": 16384` (default do template embutido para
  provedor custom).
- Correção: `custom_providers.commandcode.extra_body = {"max_completion_tokens": N}`
  (o `extra_body` é mesclado e sobrepõe o default). Com 64k o review passou a
  completar arquivos grandes; com **128k** completou os 7 arquivos.
- Observação: `--max-tokens` do OCR é teto de **prompt**; `--max-tokens-budget`
  é orçamento global. Nenhum dos dois controla o teto de saída.

## 8. Pendências

- Atualizar o PR `LizardByte/build-deps#762`: force-push da branch
  `fix/ffmpeg-patch-fallback` para `5c6bf7b` (pendente de autorização; a branch
  do PR segue em `2d41a16`).
- Responder os 3 threads da review (P1 enum, P1 output, P2 alpha) citando o
  leitor de alpha, o round-trip 1:1 e a decisão LE-only.
- (Opcional) teste em host BE real via QEMU para validar `AV_WL32`/`AV_RL32`.
