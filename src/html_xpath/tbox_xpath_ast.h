#ifndef TBOX_HTML_XPATH_AST_H
#define TBOX_HTML_XPATH_AST_H

#include <stddef.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How the set of nodes entering a step is expanded before the step's axis
 * and node test are applied. IDENTITY = just the node itself ('/' or no
 * prefix). DESCENDANT_OR_SELF = the node plus every descendant, in document
 * order ('//', the abbreviation for "/descendant-or-self::node()/"). */
typedef enum tbox_xpath_combinator {
    TBOX_XPATH_COMBINATOR_IDENTITY,
    TBOX_XPATH_COMBINATOR_DESCENDANT_OR_SELF,
} tbox_xpath_combinator;

typedef enum tbox_xpath_axis {
    TBOX_XPATH_AXIS_CHILD,
    TBOX_XPATH_AXIS_ATTRIBUTE,
    TBOX_XPATH_AXIS_SELF,   /* '.' */
    TBOX_XPATH_AXIS_PARENT, /* '..' */
} tbox_xpath_axis;

typedef enum tbox_xpath_node_test_kind {
    TBOX_XPATH_TEST_NONE,     /* SELF/PARENT: no test to apply */
    TBOX_XPATH_TEST_NAME,     /* a specific tag or attribute name */
    TBOX_XPATH_TEST_WILDCARD, /* '*' */
    TBOX_XPATH_TEST_TEXT,     /* text() */
    TBOX_XPATH_TEST_NODE,     /* node() */
} tbox_xpath_node_test_kind;

typedef enum tbox_xpath_predicate_kind {
    TBOX_XPATH_PREDICATE_POSITION,    /* [n] */
    TBOX_XPATH_PREDICATE_ATTR_EXISTS, /* [@name] */
    TBOX_XPATH_PREDICATE_ATTR_EQUALS, /* [@name='value'] */
} tbox_xpath_predicate_kind;

typedef struct tbox_xpath_predicate {
    tbox_xpath_predicate_kind kind;
    size_t position;                   /* POSITION only; 1-based */
    tbox_string_view attr_name;        /* ATTR_EXISTS/ATTR_EQUALS only; copied into the query's arena */
    tbox_string_view attr_value;       /* ATTR_EQUALS only; copied into the query's arena */
    struct tbox_xpath_predicate *next; /* in source order; applied in sequence */
} tbox_xpath_predicate;

typedef struct tbox_xpath_step {
    tbox_xpath_combinator combinator_before;
    tbox_xpath_axis axis;
    tbox_xpath_node_test_kind test_kind;
    tbox_string_view test_name;       /* only when test_kind == TBOX_XPATH_TEST_NAME; copied into the query's arena */
    tbox_xpath_predicate *predicates; /* may be NULL */
    struct tbox_xpath_step *next;
} tbox_xpath_step;

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_XPATH_AST_H */
