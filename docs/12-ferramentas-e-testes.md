# 12. Ferramentas e testes

> - **Escopo:** testes automáticos, fixtures visuais, screenshot headless, comparação contra referências, análise estática e ajudas de depuração.
> - **Código relacionado:** `tests/`, `tests/assets/`, `tests/tbox_cmp.cpp`, `tests/tbox_bench.c`, `tests/tbox_frame_dump.c`, `example/assets_showrun.c`, `example/tbox_app.c`, `include/tbox/debug.h`, `tbox.cppcheck.in`, `cmake/tbox_cppcheck_compile_db.cmake`
> - **Veja também:** [Primeiros passos](01-primeiros-passos.md), [Atualização da tela](05-atualizacao-da-tela.md#ver-o-que-está-sendo-repintado)

## Testes automáticos

```sh
ctest --test-dir build --output-on-failure
```

Um único binário, `tbox_tests`, com um grupo por módulo (arena, vector,
string, parsers HTML/CSS, seletores, cascata, cores, style, font, image,
layout, render, raster, context). `ctest` roda cada grupo como um teste;
para rodar um só: `build/tests/tbox_tests layout`.

Asserções vêm de `tests/test_support.h`. `TBOX_TEST_ASSERT` conta a falha
e segue. `TBOX_TEST_REQUIRE` conta e encerra o grupo: use-o quando as
linhas seguintes dependem da condição (tipicamente um ponteiro que será
desreferenciado), para a regressão aparecer como falha e não como segfault
do binário inteiro.

Os testes de medição de texto e layout usam a fonte embutida (Liberation
Sans), então dão o mesmo resultado em qualquer máquina, inclusive sem
fontes instaladas.

### Testes do ciclo de vida

`ctest --test-dir build -R app_lifecycle --output-on-failure` executa
`build/tests/tbox_app_lifecycle_tests`, um binário separado que compila o
loop real (`tbox_app.c`) e o módulo de eventos (`tbox_app_events.c`) contra
um backend simulado por instância. Verifica load após o
primeiro frame, foco, resize, reload, fechamento por Escape/backend/API,
limpeza dos seis registros e callbacks reentrantes, inclusive close. Também
verifica a API app de clique/input/select/submit com navegação por teclado,
valores dos controles, unbind no próprio callback, troca/limpeza de slots,
reload e liberação de userdata. Não
abre janela e não depende de fontes do sistema. É construído quando há
Fontconfig e biblioteca estática; o resto da suíte continua disponível nas
configurações mínimas. Eventos de janela não têm fixture de paridade
estática com Firefox; o exemplo interativo é `tbox_lifecycle_demo`.

## Fixtures visuais — `tests/assets/`

Arquivos `<categoria>-<tema>.html` (`layout-grid-tracks.html`,
`forms-select.html`...) agrupados por recurso: cada propriedade ou elemento
é exercitado em um só fixture, e cada fixture cabe em 800x600. O mapa
recurso → fixture, e a numeração antiga, estão em
[fixtures](referencia/fixtures.md). Os textos são em inglês. Não há imagens de referência guardadas: o `tbox_cmp` captura cada
fixture no Firefox a cada execução. Os PNGs que ficam em `tests/assets/`
(`bird.png`, `mask-star.png`...) são imagens usadas pelos próprios
fixtures.

O exemplo `tests/assets/html-tables-styled.html` reúne uma tabela de pedidos
com legenda, cabeçalho, corpo e rodapé de totais. Demonstra bordas colapsadas,
linhas alternadas, destaque ao passar o mouse, alinhamento de valores,
status coloridos e uma célula de total com `colspan`, em 800x600.

```sh
build/example/tbox_assets_showrun            # abre o primeiro; ← → navegam, Esc fecha
build/example/tbox_assets_showrun --list     # só lista a ordem
build/example/tbox_assets_showrun outro/dir  # outro diretório
```

## Screenshot headless

```c
tbox_app_screenshot_from_files("pagina.html", "pagina.css", 1024, 768, "saida.png");
```

ou, com o demo, `build/example/tbox_app_demo --screenshot saida.png`.
Não abre janela nem depende de compositor: é o frame que a janela
mostraria, escrito num PNG — já assentado: se o primeiro frame inicia uma
animação de entrada (`@starting-style`), o relógio é avançado até o fim
antes de capturar. Útil em CI e para inspecionar uma
mudança visual sem risco de capturar outra coisa da tela.

## Comparação com referências — `tbox_cmp`

```sh
build/tests/tbox_cmp tests/assets                     # todos os fixtures
build/tests/tbox_cmp --output /tmp/cmp tests/assets units-lengths cascade-layers
build/tests/tbox_cmp --size 1024x768 --firefox /opt/firefox/firefox tests/assets
```

Para cada `.html` (ou só os nomeados), o `tbox_cmp`:
1. Captura o fixture no Firefox headless (`--screenshot`), que é a referência. Usa um perfil temporário próprio, então não mexe num Firefox aberto nem no perfil do usuário, e desiste em 90 s.
2. Renderiza o fixture com `tbox_app_screenshot_from_files` no mesmo tamanho, 800x600 por padrão (`--size`).
3. Compara as duas imagens. O veredito usa checagens locais de conteúdo, bordas e cor, tolerando pequenas diferenças de glifo e posição; o SSIM é impresso para diagnóstico.

As três imagens de cada fixture ficam no diretório de saída
(`--output DIR`, ou um temporário mostrado no início):
`<nome>_firefox.png`, `<nome>_render.png` e `<nome>_diff.png`, com as diferenças
anotadas.

O Firefox vem do PATH, de `--firefox` ou de `TBOX_CMP_FIREFOX`. Sem ele,
cada fixture conta como falha operacional. O resultado da última
comparação completa está em
[Paridade com o Firefox](referencia/paridade-firefox.md).

Esse relatório lista cada HTML atual uma vez, com veredito, SSIM,
checagens locais reprovadas e observação da revisão visual. Registre também
o ambiente (build, Firefox, fontes e tamanho): os números dependem dele.
`EQUAL` admite diferenças dentro das tolerâncias e pode deixar passar um
defeito localizado; uma captura inicial não valida hover, foco, popups,
rolagem nem a evolução das animações.

- Opcional: só é construído com OpenCV (C++) e Fontconfig, e só roda com
  o Firefox instalado.
- Não faz parte do `ctest`. Vários fixtures usam features ainda não
  implementadas, e a fonte nunca é idêntica à do navegador que gerou a
  referência; `DIFFERENT` é diagnóstico, não falha. O código de saída só é
  diferente de zero em erro operacional (arquivo ausente, falha de render).

## Análise estática — cppcheck

- `cmake --build build --target cppcheck`; o target só existe se o
  cppcheck for encontrado no configure. O relatório vai para
  `build/cppcheck-report.txt`, e qualquer achado faz o target falhar.
- Configuração em `tbox.cppcheck.in`, gerado como `build/tbox.cppcheck`.
  Ele lê `build/cppcheck-compile_commands.json`, cópia do banco de
  compilação com uma entrada por fonte: o teste `app_lifecycle` compila de
  novo `src/app/tbox_app.c` e `tbox_app_events.c` (que a biblioteca já
  compila em `tbox_objects`), e sem o filtro esses arquivos seriam
  analisados duas vezes. Para rodar o cppcheck à mão, gere esse arquivo com
  o target antes. O target só existe no build do próprio tbox, não quando
  ele é subprojeto de outro.
- O addon `threadsafety` (precisa de Python 3) acusa `static` locais não
  `const` e funções da libc que não são seguras entre threads. Ele é
  desligado em `tests/` e `example/`, que são programas de uma thread só.
- Em `tests/`, `constVariablePointer` e `redundantAssignment` são
  suprimidos: a montagem dos casos sobrescreve campos de propósito.
- O relatório está zerado; mantenha assim. Falso positivo:
  `// cppcheck-suppress <id>` na linha anterior à do achado, com o motivo
  num comentário.

## Medição de desempenho — `tbox_bench`

```sh
build/tests/tbox_bench --table 1000                 # tabela sintética de 1000 linhas
build/tests/tbox_bench --table 1000 --scroll 40     # rolando 40px entre frames
build/tests/tbox_bench --virtual-table 1000000 --scroll 150  # a mesma tabela, virtual
build/tests/tbox_bench example/file-explorer/index.html example/file-explorer/styles.css
build/tests/tbox_bench --frames 50 --size 1024x768 pagina.html
```

Mede um frame com `tbox_context_run_frame`, depois de um frame de
aquecimento, e imprime a média de `--frames` frames (20 por padrão):

- **frame:** o `tbox_context_run_frame` inteiro.
- **style, layout, display list:** as três etapas medidas à parte, numa
  arena própria, com o tempo e a memória de cada uma. Ficam sem o que o
  `run_frame` faz em volta (transições, rolagem, controles), então somam
  um pouco menos que o frame.
- **damage diff e raster:** o diff do damage tracking e a repintura só das
  regiões alteradas (zero quando nada muda entre frames); entre parênteses,
  uma repintura completa da janela.
- **frame arena e resident:** a memória da `frame_arena` depois do frame e
  a residente do processo (só no Linux), com a de antes de abrir a página.

`--table N` gera uma tabela de 5 colunas com cabeçalho, zebra
(`:nth-child`), regra de `:hover`, algumas linhas com classe e a primeira
coluna em negrito, dentro de um contêiner rolável da altura da janela.
`--virtual-table N` é a mesma página com a tabela virtual
(`tbox_table_virtualize`), preenchida por um handler; para ela a linha
**document arena** mostra quanto a arena do documento cresceu durante os
frames medidos (com `--scroll`, deve ficar estável). Uma
página recebe a CSS da aplicação como segundo argumento e resolve
`<link>`, imagens e `@font-face` a partir do diretório do HTML.

O texto usa a Liberation Sans embutida, então o resultado não depende das
fontes instaladas. Os números dependem da máquina e do tipo de build:
compare só medições feitas no mesmo build (em Debug tudo fica 1,5–2×
mais lento). O `ctest` só confere que a ferramenta roda (`tool_bench`).

## Comparação entre builds — `tbox_frame_dump`

Para conferir que uma otimização não muda o resultado: gere a assinatura
de cada frame com o build de antes e com o de depois, e compare.

```sh
# antes da mudança (por exemplo, numa worktree do commit base)
git worktree add ../tbox-ref HEAD
cmake -S ../tbox-ref -B ../tbox-ref/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../tbox-ref/build --target tbox_frame_dump
../tbox-ref/build/tests/tbox_frame_dump tests/assets/*.html example/*.html > antes.txt

# depois
build/tests/tbox_frame_dump tests/assets/*.html example/*.html > depois.txt
build/tests/tbox_frame_dump --compare antes.txt depois.txt
```

Para cada documento, uma linha com uma assinatura de 64 bits por frame: o
primeiro sem ponteiro, e um por ponto de uma grade de hover (`--hover-step`,
80px por padrão), rolando o centro da janela em `--scroll` px (90) a cada
`--scroll-every` frames (9). A assinatura cobre a geometria de toda caixa e
de todo run de texto (posição e texto), cada op da display list (tipo,
retângulo e cor) e os pixels pintados. `--compare` aponta cada documento
que difere (e o primeiro frame diferente) ou que só existe de um lado; sai
com 0 quando tudo é igual, 1 quando algo difere e 2 em erro.

- A ferramenta lê campos internos do contexto (a árvore de caixas), então
  é compilada em cada árvore que se quer medir; para um commit anterior a
  ela, copie `tests/tbox_frame_dump.c` e `tests/tbox_tool_support.h` para lá.
- Como no `tbox_bench`, usa a Liberation Sans embutida e resolve
  `<link>`, imagens e `@font-face` a partir do diretório de cada HTML.
- Fixtures e exemplos (~130 documentos, ~9600 frames) levam cerca de um
  minuto em Release. Documentos que exercitam o que mudou valem mais que
  muitos documentos: ao validar o compartilhamento de estilo e o índice de
  regras (ver [Histórico](dev/historico.md)), foram usadas também tabelas
  grandes e páginas com seletores e casos de borda.
- Antes de confiar numa comparação "idêntica", vale introduzir de
  propósito o defeito que se quer evitar e ver a ferramenta acusá-lo.

## Depuração

- **Repintura parcial:** `tbox_app_set_debug_damage(app, true)` (no demo,
  `TBOX_DEBUG_DAMAGE=1`) contorna em magenta cada região repintada.
- **Log:** `include/tbox/debug.h` oferece `tbox_log_init`/`tbox_log` e
  leitura de variáveis de ambiente (`tbox_env_bool`, `tbox_env_long`) para
  os exemplos. A biblioteca em si não lê o ambiente.
- **Encerramento automático do demo:** `TBOX_WAYLAND_CLOSE_DELAY_MS=<ms>`
  fecha `tbox_app_demo` depois do tempo dado (smoke test sem pessoa).
- **Sanitizers:** `-DTBOX_ENABLE_SANITIZERS=ON`.
