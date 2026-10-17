# 12. Ferramentas e testes

> - **Escopo:** testes automáticos, fixtures visuais, screenshot headless, comparação contra referências, análise estática e ajudas de depuração.
> - **Código relacionado:** `tests/`, `tests/assets/`, `tests/tbox_cmp.cpp`, `example/assets_showrun.c`, `example/tbox_app.c`, `include/tbox/debug.h`, `tbox.cppcheck.in`, `cmake/tbox_cppcheck_compile_db.cmake`
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

## Fixtures visuais — `tests/assets/`

Arquivos `NNN.html` que exercitam uma feature cada; o README e o
[histórico](dev/historico.md) citam o número de cada um. Parte deles tem um
`NNN.png` de referência, capturado num navegador real.

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
build/tests/tbox_cmp tests/assets
```

Para cada `NNN.html` com `NNN.png` irmão, renderiza no tamanho do PNG e
compara. O veredito usa checagens locais de conteúdo, bordas e cor,
tolerando pequenas diferenças de glifo e posição; o SSIM é impresso para
diagnóstico. Imagens anotadas com as diferenças vão para o diretório
temporário mostrado no início.

- Opcional: só é construído com OpenCV (C++) e Fontconfig.
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
  compilação com uma entrada por fonte: `tbox_static` e `tbox_shared`
  compilam os mesmos arquivos, e sem o filtro cada um seria analisado duas
  vezes. Para rodar o cppcheck à mão, gere esse arquivo com o target antes.
- O addon `threadsafety` (precisa de Python 3) acusa `static` locais não
  `const` e funções da libc que não são seguras entre threads. Ele é
  desligado em `tests/` e `example/`, que são programas de uma thread só.
- Em `tests/`, `constVariablePointer` e `redundantAssignment` são
  suprimidos: a montagem dos casos sobrescreve campos de propósito.
- O relatório está zerado; mantenha assim. Falso positivo:
  `// cppcheck-suppress <id>` na linha anterior à do achado, com o motivo
  num comentário.

## Depuração

- **Repintura parcial:** `tbox_app_set_debug_damage(app, true)` (no demo,
  `TBOX_DEBUG_DAMAGE=1`) contorna em magenta cada região repintada.
- **Log:** `include/tbox/debug.h` oferece `tbox_log_init`/`tbox_log` e
  leitura de variáveis de ambiente (`tbox_env_bool`, `tbox_env_long`) para
  os exemplos. A biblioteca em si não lê o ambiente.
- **Encerramento automático do demo:** `TBOX_WAYLAND_CLOSE_DELAY_MS=<ms>`
  fecha `tbox_app_demo` depois do tempo dado (smoke test sem pessoa).
- **Sanitizers:** `-DTBOX_ENABLE_SANITIZERS=ON`.
