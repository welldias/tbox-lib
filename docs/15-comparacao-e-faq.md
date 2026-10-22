# 15. Comparação e perguntas frequentes

> - **Escopo:** onde o tbox se encaixa entre outras formas de fazer interface, e respostas curtas às dúvidas mais comuns de quem está avaliando a biblioteca.
> - **Código relacionado:** —
> - **Veja também:** [Introdução](00-introducao.md), [Limitações e roadmap](14-limitacoes-e-roadmap.md)

## Onde o tbox se encaixa

A comparação abaixo é de *abordagem*, não de qualidade: as outras opções
são maduras e o tbox é experimental.

| Abordagem | Exemplos | Diferença em relação ao tbox |
|---|---|---|
| Toolkit nativo de widgets | GTK, Qt | Interface montada com objetos widget e (no Qt) uma linguagem própria de estilo. O tbox usa HTML/CSS como descrição da UI e é uma lib C pequena, sem runtime de objetos. |
| GUI imediata | Dear ImGui, Nuklear | A aplicação redesenha a UI a cada frame em código. O tbox é *retained*: você descreve um documento e a lib decide o que redesenhar. |
| Navegador embutido | Electron, CEF, WebView | Motor web completo com JavaScript, rede e alta fidelidade, ao custo de tamanho e memória. O tbox implementa um subconjunto de HTML/CSS, sem JS e sem rede, em C puro. |
| Motor HTML leve | litehtml, Sciter, Ultralight | Mesma família de ideia. O tbox é escrito do zero em C, com o pipeline inteiro (parser, layout, raster em software, backend de janela) no mesmo projeto e testável headless. |

## Perguntas frequentes

**Posso usar JavaScript?**
Não. Toda a lógica é em C, registrando handlers por seletor CSS e mudando o
DOM. Um runtime JS com ponte para funções C é o último item do
[roadmap](14-limitacoes-e-roadmap.md).

**Funciona no Windows ou macOS?**
Ainda não há backend de janela para eles. O núcleo é C portável e roda
headless; o backend e a descoberta de fontes de cada SO são o que falta.

**Preciso de GPU?**
Não. A rasterização é toda em software, num buffer de 32 bits por pixel.

**Posso renderizar sem abrir janela?**
Sim: `tbox_app_screenshot_from_files` gera um PNG, e `tbox_context` dá a
display list para você rasterizar onde quiser. Ver
[Exemplos](04-exemplos.md#8-screenshot-sem-janela-ci).

**Posso usar meu próprio sistema de janelas ou desenhar numa textura?**
Sim, pelo `tbox_context`: você passa os eventos, gera frames e rasteriza a
display list no seu buffer, inclusive com repintura parcial. Ver
[Atualização da tela](05-atualizacao-da-tela.md#usando-o-damage-tracking-sem-tbox_app).

**É seguro usar de várias threads?**
Com uma regra: toda a API roda numa thread de UI, e as outras threads
entregam trabalho a ela com `tbox_app_post`, a única função thread-safe. Ver
[Memória e concorrência](13-memoria-e-concorrencia.md#threads).

**Por que meu clique num `<a>` não dispara o handler?**
Elementos inline não têm caixa própria; o alvo do clique é o bloco que os
contém. Dê `display: inline-block` ao elemento. Ver
[Eventos](06-eventos-e-interatividade.md#hit-test).

**Mudei o DOM e a tela não atualizou.**
Mudanças pelas funções da API são vistas sozinhas. Se você escreveu direto
num campo de `tbox_html_node`, chame `tbox_app_request_redraw(app)`; de
outra thread, mude o DOM dentro de uma função entregue por `tbox_app_post`. Ver
[Atualização da tela](05-atualizacao-da-tela.md#quando-um-frame-fica-sujo).

**Só parte do meu HTML aparece.**
Só o primeiro elemento de topo é desenhado. Envolva tudo num `<body>` ou
`<div>`. Tabelas também precisam de `</tr>`/`</td>` explícitos.

**Uma propriedade CSS não teve efeito.**
Confira [CSS suportado](referencia/css-suportado.md). Propriedades
desconhecidas são ignoradas em silêncio, como num navegador.

**Qual a licença?**
MIT (arquivo `LICENSE`). Código de terceiros em `external/` mantém a
própria licença; a fonte embutida (Liberation Sans) usa a SIL Open Font
License.
