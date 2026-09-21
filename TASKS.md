# tbox — Tarefas da v7

Quebra da seção "v7 — Listas HTML (texto em `<li>`) e tabela completa de
entidades nomeadas" do `ARCHITECTURE.md` em tarefas executáveis por
agentes sem contexto desta conversa. Cada tarefa abaixo é auto-contida:
aponta para a subseção exata do `ARCHITECTURE.md` (a fonte da verdade de
*o quê* construir) e acrescenta só o que esse documento não cobre —
caminho de arquivo, como registrar teste, como verificar que ficou
pronto.

(Este arquivo substitui a quebra de tarefas da v6, que está completa e
commitada — ver `ARCHITECTURE.md` para o design de cada camada v0-v7 e
`git log -- TASKS.md` para recuperar quebras de tarefas anteriores, se
precisar consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática nova além
das que o `ARCHITECTURE.md` já especifica explicitamente pra ela** (ver
o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`). A
v7 não especifica nenhum global/estático mutável novo — a tabela de
entidades continua `static const` (dado imutável, mesma categoria que já
existia na v6, não é o alvo desta regra). Se a implementação de alguma
tarefa parecer precisar de um global/estático MUTÁVEL, **pare e reporte
isso no relatório final da tarefa em vez de adicionar por conta própria**.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria. Nenhuma tarefa cria grupo de
teste novo nesta versão (estende os grupos já registrados `layout` e
`html_parser_tree`) — sem passo de integração de `tbox.h`/`tests/main.c`.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

A v7 não muda nenhuma assinatura pública (`include/tbox/layout.h` e
`include/tbox/html_parser.h` ficam intactos) nem nenhuma assinatura já
usada por `example/tbox_app.c` — não há quebra esperada em
`example/tbox_app_demo` antes da Tarefa 3; se algo quebrar, é uma
regressão real, não uma quebra esperada.

As Tarefas 1 e 2 abaixo não compartilham NENHUM arquivo (módulos
diferentes: `layout` vs. `html_parser`) — rodam em paralelo sem risco de
conflito de merge.

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — Layout Tree: `<li>` como text tag
**Depende de:** nada. **Bloqueia:** Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v7 — Listas HTML..." →
"Escopo" (por que só o mínimo: sem marcador, sem UA stylesheet, `<ul>`/
`<ol>` ficam fora) e "Layout Tree — `<li>` como text tag" INTEIRA
(assinatura exata da mudança e o "Fora de escopo" — listas aninhadas).
`src/layout/tbox_layout.c` — função `tbox_layout_is_text_tag` (por volta
da linha 47-58 hoje) e o comentário "Regra explícita" na seção Layout
Tree do v0 do `ARCHITECTURE.md`, pra entender o mecanismo por trás da
lista fixa (`tbox_html_node_text_content`, chamado só pros elementos
dessa lista).

**Arquivos a editar:**
- `src/layout/tbox_layout.c`: em `tbox_layout_is_text_tag`, adicionar
  `"li"` ao array `text_tags`. Nenhuma outra mudança nesse arquivo — a
  função que usa a lista (cálculo de altura, chamada de
  `tbox_html_node_text_content`, geração de text runs) já trata qualquer
  elemento da lista de forma genérica, sem `if` por tag específica.
- `tests/layout/test_layout.c`: casos novos (grupo `layout` já
  registrado, mesmo padrão do teste "5" — `<p>oi mundo</p>` — e do teste
  "6" — `<span>texto</span>` fora da lista, `text_run_count == 0`):
  - `<ul><li>oi mundo</li></ul>` → a caixa do `<li>` tem
    `text_run_count == 1` e o texto do run é `"oi mundo"` (mesma
    verificação que o teste "5" faz pra `<p>`, mas navegando até o
    `<li>`: `tbox_layout_build` do `root`, depois `first_child` até
    achar a caixa cujo `node->element.tag_name` é `"li"` — ver como
    `tests/html_parser/test_tree.c` navega a árvore em casos parecidos
    se precisar de referência de como comparar `tag_name`);
  - `<ul><li>um</li><li>dois</li><li>três</li></ul>` → três caixas de
    `<li>` irmãs, cada uma com seu próprio `text_run_count == 1` e texto
    correto (`"um"`, `"dois"`, `"três"`) — não um texto concatenado numa
    caixa só;
  - regressão: `<ul><li>texto</li></ul>` sem CSS nenhum — a caixa do
    `<ul>` continua existindo como container (`text_run_count == 0`,
    igual qualquer elemento fora da lista fixa, mesmo padrão do teste
    "6" pra `<span>`) — `<ul>` não vira text tag.

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

### Tarefa 2 — HTML Parser: tabela completa de entidades nomeadas
**Depende de:** nada. **Bloqueia:** Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v7 — Listas HTML..." →
"Escopo" (fonte da tabela: JSON oficial do WHATWG; só 1 codepoint; `;`
final continua obrigatório) e "HTML Parser — tabela completa de entidades
nomeadas" INTEIRA (a correção necessária no scanner de nome pra aceitar
dígito, e o "Fora de escopo" — multi-codepoint, legado sem `;`,
windows-1252, maiúsculas — nenhum desses entra nesta tarefa).
`src/html_parser/tbox_html_entities.c` inteiro (arquivo pequeno, ~140
linhas) — a struct `tbox_html_entity`, a tabela estática atual (~23
entradas), `tbox_html_parse_named_reference` (o scanner de nome que
precisa aceitar dígito) e `tbox_html_decode_entities` (não muda).
`tests/html_parser/test_tree.c`, testes 48-53 (entidades) — mesmo padrão
de asserção a seguir pros casos novos.

**Passo 1 — obter os dados oficiais.** Baixe
`https://html.spec.whatwg.org/entities.json` (tente `curl` via Bash
primeiro; se a rede não estiver acessível desse jeito, tente a ferramenta
`WebFetch` — é uma ferramenta "deferred", busque o schema dela via
`ToolSearch` antes de chamar; se NENHUM dos dois conseguir baixar o JSON,
**pare e reporte isso claramente no relatório final em vez de inventar
uma tabela** — não fabrique entradas). O JSON é um objeto onde cada chave
é uma referência de caractere completa, incluindo `&` e (na maioria dos
casos) `;` final (ex.: `"&amp;"`, `"&AElig;"`) — um subconjunto de ~106
nomes tem TAMBÉM uma chave irmã sem o `;` (ex. `"&amp"`) pra compatibilidade
legada. Cada valor tem um campo `"codepoints"` (array de 1 ou mais
inteiros) e `"characters"` (a string UTF-8 já codificada — não usar como
fonte, só como conferência visual; a tabela em C guarda `codepoint` como
`int`, mesmo padrão da v6, não a string pronta).

**Passo 2 — filtrar e transformar.** Da lista completa (~2231 chaves),
mantenha só as entradas cuja chave termina em `;` (descarta as ~106
variantes legadas sem `;` — mesma decisão da v6/v7 de exigir `;` sempre)
E cujo `"codepoints"` tem exatamente 1 elemento (descarta a dúzia
multi-codepoint — ver "Fora de escopo" da seção v7). Pra cada entrada
resultante, extraia o NOME sem o `&` inicial nem o `;` final (ex. a chave
`"&amp;"` vira o nome `"amp"`, igual à tabela atual) e o `codepoint`
(o único elemento do array). Uma ferramenta de linha de comando (`python3`
com o módulo `json` da stdlib, ou `jq`, o que estiver disponível no
ambiente) é o jeito mais confiável de fazer esse filtro — escreva um
script descartável (não precisa entrar no repositório, ver
"Fora de escopo" no ARCHITECTURE.md) que gera o texto C já formatado como
o array de inicializadores `{"nome", 0xNNNN}, ...`, e cole esse texto no
lugar da tabela atual em `tbox_html_entities.c`.

**Passo 3 — editar o código.**
- `src/html_parser/tbox_html_entities.c`:
  1. Substituir o array estático `tbox_html_entities[]` (linhas 16-21
     hoje, ~23 entradas) pelo array gerado no Passo 2 (~2200 entradas,
     mesma struct `tbox_html_entity { const char *name; int codepoint; }`
     — não muda o tipo). Ordene por nome (ordem alfabética) só por
     legibilidade do diff — a busca continua linear (`tbox_html_parse_named_reference`
     não muda o algoritmo de busca, só o tamanho da tabela que percorre).
     Atualize o comentário acima do array (hoje diz "~23 common named
     entities... not the full ~2000-entry table") pra refletir que agora
     É a tabela completa (ponte pra `ARCHITECTURE.md`'s v7, mesmo padrão
     de comentário que já aponta pra seção do doc em vez de repetir a
     lista inteira).
  2. Em `tbox_html_parse_named_reference`: a condição do `while` que
     varre o nome (`text.data[i] >= 'a' && text.data[i] <= 'z') ||
     (text.data[i] >= 'A' && text.data[i] <= 'Z')`) ganha uma terceira
     alternativa pra dígito (`text.data[i] >= '0' && text.data[i] <=
     '9'`) — nenhuma outra mudança na função (o resto da lógica —
     "sem letra nenhuma = falha", "sem `;` final = falha", busca linear
     na tabela — continua igual).
- `tests/html_parser/test_tree.c` — casos novos (grupo `html_parser_tree`
  já registrado, mesmo padrão dos testes 48-53):
  - `&spades;` (símbolo fora do conjunto da v6), `&alpha;` (letra grega),
    `&frac12;` (nome COM dígito — prova a correção do scanner) decodificam
    pro codepoint certo (confira o valor Unicode exato de cada um no JSON
    baixado no Passo 1, ou numa referência confiável — não adivinhe);
  - regressão: as ~23 entidades da v6 (`&amp;`, `&nbsp;`, `&mdash;`,
    `&copy;`, etc. — mesma lista do teste 48) continuam decodificando
    igual, agora vindas da tabela grande;
  - uma entidade que só existe SEM `;` no JSON original (ex. `&amp` sem
    `;`, um dos ~106 nomes legados) continua tratada como "não
    reconhecida" (texto literal) — prova de que o filtro do Passo 2
    excluiu essas chaves corretamente, mesmo padrão do teste 50 já
    existente pra referência malformada.

**Critério de pronto:** `ctest --test-dir build -R '^html_parser_tree$'`
verde + suíte inteira sem regressão.

---

## Tier 1 — fatia vertical v7

### Tarefa 3 — Fatia vertical v7 completa (exemplo + validação)
**Depende de:** Tarefa 1 e Tarefa 2 (ambas mergeadas).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v7 — Listas HTML..." →
"Fatia vertical v7 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v6, sem remover nada) — a lista `<ul><li class="v6-li-one">item 1
   ...` já existe da v6 (ver o comentário HTML sobre `.v6-li-one/-two/
   -three` explicando que o texto nunca aparecia): agora que a Tarefa 1
   fez `<li>` virar text tag, o TEXTO desses três itens deve aparecer de
   verdade dentro das caixas coloridas já existentes — atualize o
   comentário HTML acima delas pra remover a ressalva "esse texto nunca
   aparece" (não é mais verdade) e ajuste `width`/`height` fixos em
   `.v6-li-one/-two/-three` se o texto não couber mais na caixa como
   estava dimensionada (`height: 40px` pode precisar crescer, dependendo
   da altura de uma linha de texto — mesmo raciocínio de qualquer caixa
   de texto já usado em v0-v6, não invente número, confira visualmente).
   Adicione um parágrafo novo (fora da lista) usando pelo menos uma
   entidade nomeada fora do conjunto da v6 — `&spades;`, `&alpha;`,
   `&frac12;` (as mesmas da Tarefa 2, pra ligar a demo diretamente aos
   testes automatizados) — com uma classe CSS pra facilitar achar no
   screenshot (mesmo padrão de `.v6-entities` já usado na v6).
2. Nenhuma mudança de código deveria ser necessária em
   `example/tbox_app.c` (a v7 não muda nenhuma assinatura pública nem
   introduz API nova pra Application) — se algo precisar mudar, documente
   por quê no relatório final.
3. Validação: mesmo padrão das fatias verticais anteriores —
   `cmake -S . -B build && cmake --build build && ctest --test-dir build`
   tudo verde (incluindo `example/tbox_app_demo`), e, se
   `WAYLAND_DISPLAY` estiver setado (compositor real disponível), um
   smoke-test via `TBOX_WAYLAND_CLOSE_DELAY_MS` confirmando que abre,
   roda e fecha sozinho sem crash, e — mesmo padrão das fatias verticais
   anteriores — captura de tela (`grim`, se disponível) com inspeção por
   amostragem de pixel/texto confirmando: o texto dos três itens de lista
   aparece de verdade dentro de cada caixa colorida (não só a cor de
   fundo, como a v6 tinha que aceitar), e o parágrafo novo com a entidade
   fora-do-conjunto-v6 mostra o caractere decodificado (não a sintaxe
   literal `&spades;` etc.). Se `WAYLAND_DISPLAY` não estiver setado, ou
   não houver captura de tela disponível, pule esse passo e diga isso
   claramente no relatório.

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação acima roda sem erro — este é o
"pronto" da v7 inteira, não só desta tarefa.
