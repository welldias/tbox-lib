# 14. Limitações e roadmap

> - **Escopo:** o que o tbox ainda não faz e para onde ele vai. Leia antes de decidir adotá-lo.
> - **Código relacionado:** —
> - **Veja também:** [Introdução](00-introducao.md), [Débito técnico](dev/debito-tecnico.md), [CSS suportado](referencia/css-suportado.md), [HTML suportado](referencia/html-suportado.md)

## Estado do projeto

O tbox é **experimental** (versão 0.1.0). O pipeline de renderização e a
interação básica funcionam e são testados, mas a biblioteca ainda não é
adequada para uma aplicação desktop completa em produção. A API pública
ainda muda.

## Limitações que mais pesam numa decisão

| Área | Limitação |
|---|---|
| Plataforma | Só Linux com Wayland tem janela. Sem X11, Windows, macOS ou Android. |
| Scripts | Sem JavaScript; toda a lógica é em C. |
| Acessibilidade | Sem árvore de acessibilidade (leitores de tela não enxergam a UI). |
| Texto | Shaping só com HarfBuzz (opcional) e por palavra; sem bidi, sem fallback por glifo, sem IME. |
| Layout | Sem `float`, `grid`, colunas, `writing-mode` vertical, rolagem horizontal de containers. |
| CSS | Sem transformações além de translação; `mask` com uma camada só. `@import` só de arquivos locais. |
| Eventos | Elementos inline (`<a>`, `<span>`) não são alvo próprio de clique/hover; use `inline-block`. |
| Formulários | Sem `<select multiple>` ou arquivo múltiplo. |
| Recursos | Sem rede: imagens, fontes e CSS vêm de arquivos locais ou strings. |
| Desempenho | Estilo e layout são recalculados por inteiro a cada frame sujo; só a pintura é parcial. |
| Concorrência | Uma thread de UI; outras threads só entregam trabalho por `tbox_app_post`. |
| Integração | Sem `install`, `pkg-config` nem suporte a `add_subdirectory`. |
| HTML | Fechamento implícito só de `<p>`/`<li>`; feche tabelas e `<option>` explicitamente. Um único elemento de topo é desenhado. |

Simplificações de layout mais finas estão em
[Layout e pintura](08-layout-e-pintura.md#simplificações-que-você-pode-notar).

## Roadmap

SVG, áudio, vídeo e faixas de mídia (`<track>`) ficam para uma etapa futura,
após a biblioteca tbox amadurecer.

1. **Contrato de aplicação e eventos.** Fechar a API pública de
   aplicação/eventos, posse de recursos e invalidação explícita, mantendo o
   render headless independente do backend de janela.
2. **Piloto Linux.** Uma aplicação com formulário e lista rolável: foco,
   teclado e entrada de texto, botões, edição, seleção, rolagem, recorte e
   persistência simples via callbacks C.
3. **CSS que o piloto precisa.** Layout e pintura necessários a ele,
   incluindo dimensionamento adaptativo e hit-test correto em conteúdo
   recortado.
4. **Windows e macOS.** Backends nativos e alvos de distribuição Linux,
   validando DPI, clipboard, acessibilidade, empacotamento e ciclo de vida em
   cada um.
5. **JavaScript.** Um runtime com ponte explícita para funções C
   registradas. A API de eventos por seletor e de mutação do DOM já foi
   desenhada no formato que esse runtime usaria.

Cada marco deve trazer testes de comportamento, um exemplo executável e
critérios de aceitação por plataforma.

## Próximos passos técnicos já mapeados

- Estilo e layout incrementais (invalidação por nó).
- Fechamento implícito de `<tr>`/`<td>`/`<th>`/`<option>`.

Motivo e gatilho de cada item: [Débito técnico](dev/debito-tecnico.md).
