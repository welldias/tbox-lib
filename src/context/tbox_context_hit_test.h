#ifndef TBOX_CONTEXT_HIT_TEST_H
#define TBOX_CONTEXT_HIT_TEST_H

#include <tbox/layout.h>

/* Exposes tbox_context_hit_test_box (otherwise only reachable through the
 * opaque tbox_context via the public tbox_context_hit_test) so
 * tests/context/test_context.c can drive the hit-test recursion directly
 * against a tbox_layout_box tree it builds by hand -- no HTML/CSS parse,
 * no Style/Layout pipeline -- the same "exercise the internal algorithm
 * through a private header, not just through the public wrapper" pattern
 * already used by other modules that need this (e.g.
 * src/css_parser/tbox_css_tokenizer.h, included directly by
 * tests/css_parser/test_tokenizer.c).
 *
 * See tbox_context.c's doc comment on the function itself (and
 * ARCHITECTURE.md's "Orchestration -- correção de hit-test pra caixas fora
 * de fluxo") for what it actually does; this header only exists to give it
 * external linkage for that reason. Not declared in <tbox/context.h> --
 * `box` is an implementation detail of tbox_context_hit_test, not part of
 * the public API. */
const tbox_layout_box *tbox_context_hit_test_box(const tbox_layout_box *box, double x, double y);

#endif /* TBOX_CONTEXT_HIT_TEST_H */
