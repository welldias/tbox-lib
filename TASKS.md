# tbox — Tarefas da v12

Quebra da seção "v12 — CSS `font-family`" do `ARCHITECTURE.md` em tarefas
executáveis por agentes sem contexto desta conversa. Cada tarefa abaixo é
auto-contida: aponta para a subseção exata do `ARCHITECTURE.md` (a fonte
da verdade de *o quê* construir) e acrescenta só o que esse documento não
cobre — caminho de arquivo, como registrar teste, como verificar que
ficou pronto.

(Este arquivo substitui a quebra de tarefas da v11, que está completa —
ver `ARCHITECTURE.md` para o design de cada camada v0-v12 e `git log --
TASKS.md` para recuperar quebras de tarefas anteriores, se precisar
consultá-las. O plano completo desta versão, incluindo o raciocínio de
design por trás de cada decisão, também está em
`/home/welldias/.claude/plans/vamos-trabalhar-no-font-family-modular-nautilus.md`,
gerado em modo de plano com um agente de pesquisa e um agente de design
dedicados — o `ARCHITECTURE.md` é a versão consolidada/canônica, mas o
plano tem contexto adicional se precisar.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática MUTÁVEL nova
além das que o `ARCHITECTURE.md` já especifica explicitamente pra ela**
(ver o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`).
A v12 não especifica nenhum global/estático mutável novo — se a
implementação de alguma tarefa parecer precisar de um, **pare e reporte
isso no relatório final da tarefa em vez de adicionar por conta própria**.

**Escopo travado, não expanda em nenhuma tarefa:** só o PRIMEIRO nome de
uma lista `font-family` separada por vírgula é usado (decisão confirmada
com o mantenedor); sem `@font-face`; sem `italic`/peso numérico (toda
resolução de fonte continua com `italic = false`); sem fallback por
glifo; sem limite/eviction do cache de fontes.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**As Tarefas 1, 2 e 3 (Tier 0) não compartilham NENHUM arquivo** (módulos
diferentes: `font`, `style`, `context`) — rodam em paralelo sem risco de
conflito de merge. **A Tarefa 4 depende só da Tarefa 1** (precisa da nova
assinatura de `tbox_font_face_cache_create`/`_get`). **A Tarefa 5 depende
da Tarefa 1 E da Tarefa 2** (precisa da nova assinatura do cache E do
campo `tbox_style.font_family`). Tarefas 4 e 5 não compartilham arquivo
entre si — podem rodar em paralelo uma vez que 1 e 2 estejam prontas.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

**Mudança de assinatura pública, não é quebra silenciosa:**
`tbox_font_face_cache_create` ganha dois parâmetros novos (`resolver`,
`resolver_userdata`) e `tbox_font_face_cache_get` ganha um parâmetro novo
(`family`, como primeiro argumento depois de `cache`) — todo call site
existente (produção E testes) precisa ser atualizado. A Tarefa 1 já lista
os arquivos de teste afetados; as Tarefas 4 e 5 têm call sites de
produção próprios pra atualizar.

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — Font: cache com resolução sob demanda
**Depende de:** nada. **Bloqueia:** Tarefa 4, Tarefa 5.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v12 — CSS `font-family`" →
"Escopo" (por que buffer fixo não se aplica aqui — essa parte é sobre
`tbox_style`, não sobre o cache — e por que callback de resolução, não
fontconfig direto) e "Font — cache com resolução sob demanda" INTEIRA
(a struct exata, o vetor `family_blobs`, o comportamento de
`family.size == 0`). `include/tbox/font.h` inteiro — em especial
`tbox_font_query`, `tbox_font_face_cache_create`/`_get` (as duas
assinaturas que crescem) e suas doc comments (que precisam ser
corrigidas, ficam desatualizadas). `src/font/tbox_font_face_cache.c`
inteiro (116 linhas, arquivo pequeno) — a struct `tbox_font_face_cache`,
`tbox_font_face_cache_entry`, e o algoritmo exato de
`tbox_font_face_cache_get` (busca linear, miss cai pro `bold ?
bold_data : regular_data` — esse é o ÚNICO ponto que hardcoda uma família
só, hoje).

**Arquivos a editar:**
- `include/tbox/font.h`:
  1. Novo typedef `tbox_font_resolver_fn` (assinatura exata no
     ARCHITECTURE.md) — um ponteiro de função que recebe `userdata` +
     `tbox_font_query` e escreve `out_data`/`out_size` em sucesso, mesmo
     contrato de retorno que `tbox_font_source_resolve` já tem.
  2. `tbox_font_face_cache_create` ganha `tbox_font_resolver_fn resolver,
     void *resolver_userdata` como dois parâmetros novos no final.
  3. `tbox_font_face_cache_get` ganha `tbox_string_view family` como
     PRIMEIRO parâmetro depois de `cache` (antes de `bold`).
  4. Corrija a doc comment de `tbox_font_query` (hoje diz "v0 only
     understands generics such as 'sans-serif', never a specific font
     name" — isso deixa de ser verdade) e a de `tbox_font_face_cache`
     (hoje diz "keyed by (bold, size_px)" — passa a ser "(family, bold,
     size_px)").
- `src/font/tbox_font_face_cache.c`:
  1. `struct tbox_font_face_cache` ganha `tbox_font_resolver_fn resolver;
     void *resolver_userdata;` (guardados direto do que
     `tbox_font_face_cache_create` recebe) e um vetor novo `tbox_vector
     family_blobs;` — elemento `{ char family[64]; bool bold; const void
     *data; size_t size; }`, inicializado junto de `entries` no mesmo
     `cache->arena`.
  2. `tbox_font_face_cache_entry` ganha `char family[64];` (preenchido
     com `""` — `memset`/`[0] = '\0'` — nas entradas do caminho default
     já existente).
  3. `tbox_font_face_cache_get`: copie `family` pra um buffer local de 64
     bytes, truncando e NUL-terminando (mesma postura de
     `tbox_font_source_fontconfig_family_cstr`, que você pode usar como
     referência de estilo mesmo estando em outro arquivo). A busca linear
     em `entries` ganha a comparação de `family` (via `strcmp` contra o
     buffer local) além de `bold`/`size_px` já existentes. No caminho de
     MISS: se `family` está vazia, o comportamento é EXATAMENTE o de
     hoje (`bold ? bold_data : regular_data`). Se `family` não está
     vazia: procure em `family_blobs` por `(family, bold)`; se não achar,
     e `cache->resolver != NULL`, chame o resolver com `{family, bold,
     false}`; se retornar `true`, copie os bytes resolvidos pro
     `cache->arena` (mesmo helper `tbox_font_face_cache_copy_bytes` já
     existente) e empurre uma entrada nova em `family_blobs`; se o
     resolver for `NULL` ou retornar `false`, `tbox_font_face_cache_get`
     retorna `NULL` sem cachear nada (mesmo contrato de falha que já
     existe). Com os bytes em mãos (do default OU de `family_blobs`),
     `tbox_font_face_load` continua exatamente como hoje.
  4. `tbox_font_face_cache_create` passa a guardar `resolver`/
     `resolver_userdata` nos campos novos da struct.
- `tests/font/test_font.c` — TODOS os call sites existentes de
  `tbox_font_face_cache_create`/`tbox_font_face_cache_get` precisam do
  argumento novo (`create` ganha `, NULL, NULL` no final — sem resolver,
  mesmo comportamento de hoje; `get` ganha `tbox_string_view_make(NULL,
  0)` como primeiro argumento — família vazia = comportamento de hoje).
  Casos NOVOS (grupo `font` já registrado, mesmo padrão dos testes 7-14
  já existentes como referência — miss/hit, chaves distintas, `NULL`
  seguro):
  - crie um `tbox_font_face_cache` com um resolver de TESTE (uma função
    `static` local no arquivo de teste, não em produção) que devolve os
    MESMOS bytes vendorizados pra qualquer família pedida, mas grava
    (numa variável local de pilha do teste, passada como
    `resolver_userdata`) quantas vezes foi chamada;
  - `_get(cache, "Alguma Familia", false, 16.0)` não retorna `NULL`, e o
    contador de chamadas do resolver foi incrementado uma vez;
  - uma SEGUNDA chamada `_get(cache, "Alguma Familia", false, 16.0)`
    retorna o MESMO ponteiro E o contador do resolver NÃO incrementou de
    novo (cache hit, sem re-resolver);
  - `_get(cache, "Alguma Familia", false, 32.0)` (mesmo família/peso,
    tamanho diferente) retorna um ponteiro DIFERENTE, mas o contador do
    resolver TAMBÉM não incrementa de novo (reusa o blob já resolvido pra
    esse `(família, peso)`, só carrega um `tbox_font_face` novo pro
    tamanho novo — prova do `family_blobs`);
  - um cache criado com `resolver = NULL` (mesmo padrão de hoje) chamado
    com uma família NÃO-vazia retorna `NULL` (sem crash);
  - regressão: `_get(cache, tbox_string_view_make(NULL, 0), false, 16.0)`
    (família vazia) continua funcionando exatamente como antes, mesmo com
    um resolver configurado — o caminho default nunca invoca o resolver.

**Critério de pronto:** `ctest --test-dir build -R '^font$'` verde +
suíte inteira sem regressão.

### Tarefa 2 — Style: propriedade `font-family`
**Depende de:** nada. **Bloqueia:** Tarefa 5.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v12 — CSS `font-family`" →
"Escopo" (por que buffer fixo, não `tbox_string_view` — o problema de
lifetime do `style=""` sintético da v9) e "Style — `font-family`"
INTEIRA. `include/tbox/style.h` — a struct `tbox_style` (onde o campo
novo entra) e `tbox_style_parse_text_align`/o bloco de resolução de
`text-align` em `tbox_style_resolve` (o padrão mais próximo — três ramos:
declaração reconhecida, herda do pai, valor inicial — a replicar).
`src/style/tbox_style.c` inteiro.

**Arquivos a editar:**
- `include/tbox/style.h`: `tbox_style` ganha `char font_family[64];`
  (comentário: `""` = sem override em toda a cadeia de herança).
  Atualize a doc comment de "Scope" de `tbox_style_resolve` pra
  mencionar `font-family` como suportado (só o primeiro nome de uma
  lista, aspas removidas).
- `src/style/tbox_style.c`:
  1. Nova função `static bool tbox_style_parse_font_family(tbox_string_view
     raw, char *out, size_t out_capacity)` — algoritmo exato no
     ARCHITECTURE.md: se o valor (já trimado) começa com `"` ou `'`, ache
     o fechamento correspondente e use o texto entre as aspas; senão, ache
     a primeira `,` e use o texto antes dela (ou o valor inteiro, se não
     houver vírgula). `tbox_style_trim` no resultado. Copie pra `out`
     truncando em `out_capacity - 1` bytes, sempre NUL-terminando. Retorna
     `false` se o resultado (depois de trim) ficar vazio.
  2. Em `tbox_style_resolve`: resolva `font-family` com o MESMO padrão de
     três ramos que `text-align`/`font-weight` já usam — declaração
     `font-family` reconhecida → usa o valor parseado (copiado pro
     buffer de `style.font_family`); senão, se há `parent_style`, copia
     `parent_style->font_family` (`memcpy` do buffer inteiro, mais
     simples que `strcpy` e sempre correto já que ambos são
     `char[64]`); senão, `style.font_family[0] = '\0';` explícito.
- `tests/style/test_style.c` — casos novos (grupo `style` já registrado,
  mesmo padrão `resolve_node`/`TBOX_TEST_ASSERT` já usado):
  - `div { font-family: Verdana; }` resolve `style.font_family` igual a
    `"Verdana"` (compare com `strcmp`);
  - `div { font-family: "Courier New", monospace; }` resolve
    `"Courier New"` (só o primeiro nome, aspas removidas, vírgula DENTRO
    das aspas não corta no lugar errado);
  - `div { font-family: Verdana, Arial, sans-serif; }` (sem aspas, lista
    com múltiplos nomes) resolve só `"Verdana"`;
  - um `<div><p>x</p></div>` com `div { font-family: Verdana; }` e `p`
    SEM `font-family` própria — o `p` herda `"Verdana"` do pai;
  - regressão: um `div` sem `font-family` nenhuma, sem pai, resolve
    string vazia (`style.font_family[0] == '\0'`).

**Critério de pronto:** `ctest --test-dir build -R '^style$'` verde +
suíte inteira sem regressão.

### Tarefa 3 — Orchestration: `<pre>` monoespaçado via UA stylesheet
**Depende de:** nada. **Bloqueia:** nada (a fatia vertical depende dela,
nenhuma outra tarefa).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v12 — CSS `font-family`" →
"Orchestration — `<pre>` monoespaçado via UA stylesheet" INTEIRA — é uma
linha só de template, sem campo novo em `tbox_ua_style_config` (não
precisa de `%g`, é texto literal). `src/context/tbox_context.c` —
`tbox_ua_style_generate_css` (onde a linha entra, mesmo lugar das regras
de `hr`/`li` já existentes).

**Arquivos a editar:**
- `src/context/tbox_context.c`: adicione ao template `snprintf` de
  `tbox_ua_style_generate_css` a linha
  `"pre { display: block; font-family: monospace; }\n"` — sem argumento
  `%g` novo (não mexe na lista de argumentos do `snprintf`, só no texto
  do template). **Não precisa** de nenhuma mudança em
  `include/tbox/context.h` nem em `tbox_ua_style_config_default()`.
- `tests/context/test_context.c` — caso novo (grupo `context` já
  registrado): um documento com `<pre>x</pre>` (sem CSS de autor) —
  confirme que o estilo resolvido desse nó tem `font_family` igual a
  `"monospace"` (via `tbox_style_table`, mesmo padrão de inspeção que os
  testes de UA stylesheet da v8/v11 já usam). **Esta tarefa depende da
  Tarefa 2 já ter mergeado o campo `tbox_style.font_family` pra esse
  teste específico compilar** — se a Tarefa 2 ainda não estiver pronta
  quando você for escrever ESSE teste específico, escreva o resto da
  tarefa (a linha do template) e deixe esse teste como último passo,
  ou adicione depois que a Tarefa 2 mergear. A MUDANÇA em
  `tbox_context.c` em si não depende de nada.

**Critério de pronto:** `ctest --test-dir build -R '^context$'` verde +
suíte inteira sem regressão (o teste específico de `font_family ==
"monospace"` só passa depois que a Tarefa 2 também estiver mergeada —
tudo bem se isso acontecer numa ordem diferente, contanto que o teste
seja adicionado e passe antes da fatia vertical).

---

## Tier 1 — depende de Tarefa 1 (e, pra Tarefa 5, também da Tarefa 2)

### Tarefa 4 — Application: resolver sob demanda + de-duplicação
**Depende de:** Tarefa 1 (mergeada — precisa da nova assinatura de
`tbox_font_face_cache_create`). **Bloqueia:** Tarefa 6.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v12 — CSS `font-family`" →
"Application — resolução sob demanda + limpeza de duplicação" INTEIRA.
`src/app/tbox_app.c` inteiro — em especial `tbox_app_resolve_font_source`
(o padrão a reaproveitar pro resolver novo), `tbox_app_create_impl`
(onde a sequência "resolve regular, resolve bold, `_cache_create`" vive
hoje) e `tbox_app_screenshot_from_files` (que tem uma cópia INDEPENDENTE
da mesma sequência, por volta das linhas 271-336 — confirme a localização
exata no arquivo atual, pode ter mudado de linha desde então).

**Arquivos a editar:**
- `src/app/tbox_app.c`:
  1. Nova função `static bool tbox_app_font_resolver(void *userdata,
     tbox_font_query query, const void **out_data, size_t *out_size)` —
     ignora `userdata` (não precisa de nada nele, passe `NULL` em todo
     lugar que a cria), cria um `tbox_font_source_fontconfig` fresco,
     resolve `query` nele, copia `out_data`/`out_size` do resultado,
     destrói a source, retorna `true`/`false` conforme o resolve teve
     sucesso — exatamente o que `tbox_app_resolve_font_source` já faz por
     chamada, só que essa função nova recebe um `tbox_font_query`
     completo (incluindo a família) em vez de só um `bool bold`.
  2. **De-duplicação recomendada**: extraia a sequência "resolve
     regular, resolve bold via `tbox_app_resolve_font_source`,
     `tbox_font_face_cache_create`" (hoje duplicada em
     `tbox_app_create_impl` E `tbox_app_screenshot_from_files`) pra uma
     função só, `static tbox_font_face_cache *tbox_app_build_font_cache(void)`,
     que os dois passam a chamar. Essa função passa
     `tbox_app_font_resolver, NULL` como os dois argumentos novos de
     `tbox_font_face_cache_create`.
  3. Ajuste as DUAS chamadas a `tbox_font_face_cache_create` (agora só
     uma, dentro de `tbox_app_build_font_cache`, se você fez a
     de-duplicação do item 2 — ou duas, se preferir não de-duplicar,
     mas a de-duplicação é o caminho recomendado já que ambos os pontos
     precisam mudar de qualquer forma).

Escopo EXATO (não expanda): não mude `tbox_app_resolve_font_source` em si
(continua existindo, usado só pelo caminho eager de regular/bold — o
resolver novo é uma função DIFERENTE, não uma substituição). Não toque em
`include/tbox/app.h` (nenhuma assinatura pública de `tbox_app_*` muda).

**Critério de pronto:** `ctest --test-dir build` inteiro sem regressão
(esta tarefa não tem grupo de teste próprio — `tbox_app.c` só é exercido
indiretamente via `example/tbox_app_demo`, sem testes automatizados
dedicados, mesmo padrão de sempre). Além do `ctest`, rode
`./build/example/tbox_app_demo --screenshot /tmp/qualquer.png` e confirme
que ainda funciona (exit 0, PNG válido) — prova de que a de-duplicação/
o resolver novo não quebrou o caminho default (família vazia).

### Tarefa 5 — Layout Tree: os 6 pontos de chamada
**Depende de:** Tarefa 1 E Tarefa 2 (ambas mergeadas — precisa da nova
assinatura de `tbox_font_face_cache_get` E do campo
`tbox_style.font_family`). **Bloqueia:** Tarefa 6.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v12 — CSS `font-family`" →
"Layout Tree — os 6 pontos de chamada" INTEIRA (a lista exata das 6
chamadas e qual `style` cada uma usa). `src/layout/tbox_layout.c` inteiro
— procure TODA ocorrência de `tbox_font_face_cache_get` (são 6, todas
passando só `(fonts, bold, size)` hoje).

**Arquivos a editar:**
- `src/layout/tbox_layout.c`: cada uma das 6 chamadas a
  `tbox_font_face_cache_get` ganha `tbox_string_view_from_cstr(style-
  >font_family)` (ou `child_style->font_family` na chamada de dentro de
  `tbox_layout_collect_words` que já usa `child_style` pra tudo o resto —
  use exatamente o mesmo `style`/`child_style` que a chamada JÁ usa pra
  `bold`/`size`, nunca um diferente) como o argumento novo, na posição
  certa (depois de `fonts`, antes de `bold` — confirme a ordem exata
  contra a assinatura que a Tarefa 1 define em `include/tbox/font.h`).
- `tests/layout/test_layout.c` — os call sites diretos de
  `tbox_font_face_cache_get` (existem alguns, usados pra comparar contra
  `text_runs[i].font`) também ganham o argumento novo (família vazia,
  `tbox_string_view_make(NULL, 0)`, pra manter o comportamento de teste
  de hoje — a menos que você esteja escrevendo um teste NOVO
  especificamente sobre família, aí use a família de verdade que o teste
  está exercitando). Casos novos:
  - `<p style="font-family: Verdana;">x</p>` → o `text_runs[0].font`
    retornado é o MESMO ponteiro que
    `tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr("Verdana"),
    false, 16.0)` retornaria diretamente (prova de que a família chega
    até o Layout Tree e é usada na hora de escolher a face) — você vai
    precisar de um `tbox_font_face_cache` criado com um resolver de teste
    (mesmo padrão da Tarefa 1) pra esse teste funcionar sem depender de
    fontconfig de verdade;
  - regressão: um `<p>` sem `font-family` nenhuma continua resolvendo a
    MESMA face de antes (família vazia, caminho default).

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

---

## Tier 2 — fatia vertical v12

### Tarefa 6 — Fatia vertical v12 completa (exemplo + validação)
**Depende de:** Tarefas 1, 2, 3, 4 e 5 (todas mergeadas).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v12 — CSS `font-family`" →
"Fatia vertical v12 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v11, sem remover nada) com:
   - Um elemento novo com `font-family: verdana;` (ou outra família
     instalada de verdade no ambiente de build — confirme com `fc-list`
     ou similar se tiver dúvida) via `style=""` ou classe CSS nova
     (`.v12-*`, mesmo padrão de toda demo anterior), com texto
     suficiente pra a diferença de fonte ficar visível na screenshot.
   - Um `<pre>` NOVO (ou aproveitando o `.v11-pre-demo` já existente, se
     fizer sentido) SEM `font-family` declarado no HTML — deve mostrar
     fonte monoespaçada só pelo default da UA stylesheet (Tarefa 3).
   - Um elemento com `font-family` próprio contendo um `<b>`/`<em>`
     aninhado sem `font-family` — o filho deve herdar a família do pai
     (prova visual de herança, mesmo texto/formato de fonte no filho).
2. Nenhuma mudança de código deveria ser necessária em `example/tbox_app.c`
   além de possivelmente `TBOX_APP_DEMO_HEIGHT` (mesmo padrão de todo
   incremento anterior — confira visualmente via `--screenshot` antes de
   mudar o número).
3. Validação: `cmake -S . -B build && cmake --build build && ctest
   --test-dir build` tudo verde primeiro (22 grupos hoje — 21 + `font`
   sem mudar de nome, os grupos existentes só ganharam mais casos).
   `./tbox_app_demo --screenshot <path>.png` — confira visualmente: (a) o
   elemento com `font-family: verdana` mostra glifos visivelmente
   diferentes do sans-serif default; (b) o `<pre>` sem `font-family`
   próprio mostra fonte monoespaçada; (c) o filho `<b>`/`<em>` aninhado
   herda a família do pai; (d) nada de v0-v11 foi cortado/alterado.
4. Opcional, recomendado: rode `./build/tests/tbox_cmp tests/assets`
   manualmente (se `TBOX_OPENCV_FOUND` disponível) e confira se
   `010.html` (que já usa `font-family:verdana;`/`courier;`) melhorou de
   SSIM em relação à v11 — não é critério de aprovação, só documentação
   (a fonte ainda não bate 100% contra o golden real, capturado de um
   browser de verdade).

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação visual acima confirma os três
itens (font-family aplicado, `<pre>` monoespaçado, herança) sem erro —
este é o "pronto" da v12 inteira, não só desta tarefa.
