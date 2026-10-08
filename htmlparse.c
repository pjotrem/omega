#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
 *  biblioteka: parser HTML
 * ============================================================ */

/* ---------- typy ---------- */

typedef struct Attr {
    char *name;
    char *value;
    struct Attr *next;
} Attr;

typedef struct Node {
    char *tag;          /* NULL = węzeł tekstowy */
    char *text;         /* treść tekstowa */
    Attr *attrs;
    struct Node *parent;
    struct Node *children;
    struct Node *next;
} Node;

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    Node *root;
    Node *current;
} Parser;

/* ---------- pomocnicze ---------- */

static char *xstrndup(const char *s, size_t n) {
    char *r = malloc(n + 1);
    if (!r) { perror("malloc"); exit(1); }
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static int is_name_char(int c) {
    return isalnum(c) || c == '-' || c == '_' || c == ':';
}

static void skip_ws(Parser *p) {
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos]))
        p->pos++;
}

/* ---------- drzewo ---------- */

static Node *node_new_text(const char *s, size_t len) {
    Node *n = calloc(1, sizeof(Node));
    if (!n) { perror("calloc"); exit(1); }
    n->text = xstrndup(s, len);
    return n;
}

static Node *node_new_tag(const char *tag, size_t len) {
    Node *n = calloc(1, sizeof(Node));
    if (!n) { perror("calloc"); exit(1); }
    n->tag = xstrndup(tag, len);
    return n;
}

static void node_append(Node *parent, Node *child) {
    child->parent = parent;
    if (!parent->children) {
        parent->children = child;
    } else {
        Node *c = parent->children;
        while (c->next) c = c->next;
        c->next = child;
    }
}

static void free_tree(Node *n) {
    while (n) {
        Node *next = n->next;
        free_tree(n->children);
        free(n->tag);
        free(n->text);
        Attr *a = n->attrs;
        while (a) {
            Attr *na = a->next;
            free(a->name);
            free(a->value);
            free(a);
            a = na;
        }
        free(n);
        n = next;
    }
}

/* ---------- encje ---------- */

static const char *decode_entity(const char *s, size_t *consumed) {
    if (strncmp(s, "&amp;",  5) == 0) { *consumed = 5; return "&"; }
    if (strncmp(s, "&lt;",   4) == 0) { *consumed = 4; return "<"; }
    if (strncmp(s, "&gt;",   4) == 0) { *consumed = 4; return ">"; }
    if (strncmp(s, "&quot;", 6) == 0) { *consumed = 6; return "\""; }
    if (strncmp(s, "&apos;", 6) == 0) { *consumed = 6; return "'"; }
    if (strncmp(s, "&nbsp;", 6) == 0) { *consumed = 6; return " "; }
    return NULL;
}

/* ---------- parser ---------- */

static int is_void_element(const char *tag) {
    static const char *voids[] = {
        "area","base","br","col","embed","hr","img","input",
        "link","meta","param","source","track","wbr", NULL
    };
    for (int i = 0; voids[i]; i++)
        if (strcmp(tag, voids[i]) == 0) return 1;
    return 0;
}

static size_t read_name(Parser *p) {
    size_t start = p->pos;
    while (p->pos < p->len && is_name_char((unsigned char)p->src[p->pos]))
        p->pos++;
    return p->pos - start;
}

static Attr *parse_attrs(Parser *p) {
    Attr *head = NULL, *tail = NULL;
    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len) break;
        char c = p->src[p->pos];
        if (c == '>' || c == '/') break;

        size_t nlen = read_name(p);
        if (nlen == 0) { p->pos++; continue; }

        Attr *a = calloc(1, sizeof(Attr));
        if (!a) { perror("calloc"); exit(1); }
        a->name  = xstrndup(p->src + p->pos - nlen, nlen);
        a->value = xstrndup("", 0);

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == '=') {
            p->pos++;
            skip_ws(p);
            if (p->pos < p->len) {
                char q = p->src[p->pos];
                if (q == '"' || q == '\'') {
                    p->pos++;
                    size_t vstart = p->pos;
                    while (p->pos < p->len && p->src[p->pos] != q)
                        p->pos++;
                    free(a->value);
                    a->value = xstrndup(p->src + vstart, p->pos - vstart);
                    if (p->pos < p->len) p->pos++;
                } else {
                    size_t vstart = p->pos;
                    while (p->pos < p->len &&
                           !isspace((unsigned char)p->src[p->pos]) &&
                           p->src[p->pos] != '>')
                        p->pos++;
                    free(a->value);
                    a->value = xstrndup(p->src + vstart, p->pos - vstart);
                }
            }
        }
        if (tail) { tail->next = a; tail = a; }
        else      { head = tail = a; }
    }
    return head;
}

static void parse_start_tag(Parser *p) {
    size_t nlen = read_name(p);
    if (nlen == 0) { p->pos++; return; }

    Node *n = node_new_tag(p->src + p->pos - nlen, nlen);
    n->attrs = parse_attrs(p);

    int self_close = 0;
    if (p->pos < p->len && p->src[p->pos] == '/') { self_close = 1; p->pos++; }
    if (p->pos < p->len && p->src[p->pos] == '>') p->pos++;

    node_append(p->current, n);
    if (!self_close && !is_void_element(n->tag))
        p->current = n;
}

static void parse_end_tag(Parser *p) {
    p->pos++; /* pomiń '/' */
    size_t nlen = read_name(p);
    char *name = xstrndup(p->src + p->pos - nlen, nlen);

    while (p->pos < p->len && p->src[p->pos] != '>') p->pos++;
    if (p->pos < p->len) p->pos++;

    Node *cur = p->current;
    while (cur && cur != p->root) {
        if (cur->tag && strcmp(cur->tag, name) == 0) {
            p->current = cur->parent;
            free(name);
            return;
        }
        cur = cur->parent;
    }
    free(name);
}

static void parse_text(Parser *p) {
    size_t start = p->pos;
    while (p->pos < p->len && p->src[p->pos] != '<')
        p->pos++;
    if (p->pos == start) return;

    size_t cap = (p->pos - start) + 1;
    char *buf = malloc(cap);
    if (!buf) { perror("malloc"); exit(1); }
    size_t out = 0;
    for (size_t i = start; i < p->pos; ) {
        if (p->src[i] == '&') {
            size_t consumed = 0;
            const char *rep = decode_entity(p->src + i, &consumed);
            if (rep) {
                size_t rl = strlen(rep);
                if (out + rl + 1 > cap) {
                    cap = (out + rl + 1) * 2;
                    buf = realloc(buf, cap);
                    if (!buf) { perror("realloc"); exit(1); }
                }
                memcpy(buf + out, rep, rl);
                out += rl;
                i += consumed;
                continue;
            }
        }
        if (out + 2 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) { perror("realloc"); exit(1); }
        }
        buf[out++] = p->src[i++];
    }
    buf[out] = '\0';

    int only_ws = 1;
    for (size_t i = 0; i < out; i++)
        if (!isspace((unsigned char)buf[i])) { only_ws = 0; break; }

    if (!only_ws) {
        Node *n = node_new_text(buf, out);
        node_append(p->current, n);
    }
    free(buf);
}

static void parse_comment(Parser *p) {
    if (p->pos + 2 < p->len &&
        p->src[p->pos] == '!' &&
        p->src[p->pos+1] == '-' &&
        p->src[p->pos+2] == '-') {
        p->pos += 3;
        while (p->pos + 2 < p->len) {
            if (p->src[p->pos] == '-' &&
                p->src[p->pos+1] == '-' &&
                p->src[p->pos+2] == '>') {
                p->pos += 3;
                return;
            }
            p->pos++;
        }
        p->pos = p->len;
    } else {
        while (p->pos < p->len && p->src[p->pos] != '>') p->pos++;
        if (p->pos < p->len) p->pos++;
    }
}

static Node *parse_document(const char *src) {
    Parser p = { src, 0, strlen(src), NULL, NULL };
    Node *root = calloc(1, sizeof(Node));
    if (!root) { perror("calloc"); exit(1); }
    root->tag = xstrndup("#document", 9);
    p.root = root;
    p.current = root;

    while (p.pos < p.len) {
        if (p.src[p.pos] == '<') {
            if (p.pos + 1 < p.len && p.src[p.pos + 1] == '/') {
                p.pos++;
                parse_end_tag(&p);
            } else if (p.pos + 1 < p.len && p.src[p.pos + 1] == '!') {
                p.pos++;
                parse_comment(&p);
            } else if (p.pos + 1 < p.len && p.src[p.pos + 1] == '?') {
                while (p.pos < p.len && p.src[p.pos] != '>') p.pos++;
                if (p.pos < p.len) p.pos++;
            } else {
                p.pos++;
                parse_start_tag(&p);
            }
        } else {
            parse_text(&p);
        }
    }
    return root;
}

/* ---------- wypisanie drzewa ---------- */

static void print_indent(int n) {
    for (int i = 0; i < n; i++) putchar(' ');
}

static void print_tree(Node *n, int depth) {
    for (; n; n = n->next) {
        print_indent(depth);
        if (n->tag) {
            printf("<%s", n->tag);
            for (Attr *a = n->attrs; a; a = a->next)
                printf(" %s=\"%s\"", a->name, a->value);
            printf(">\n");
        } else {
            printf("#text: \"%s\"\n", n->text);
        }
        print_tree(n->children, depth + 2);
    }
}

/* ============================================================
 *  demo / CLI
 * ============================================================ */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return NULL; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) { perror("ftell"); fclose(f); return NULL; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { perror("malloc"); fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    char *src;

    if (argc >= 2) {
        src = read_file(argv[1]);
        if (!src) return 1;
    } else {
        const char *demo =
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head><title>Test</title></head>\n"
            "<body>\n"
            "  <h1 class=\"title\">Hello &amp; welcome</h1>\n"
            "  <p>This is <b>bold</b> and <i>italic</i>.</p>\n"
            "  <br>\n"
            "  <!-- komentarz -->\n"
            "  <img src=\"x.png\" alt='y'>\n"
            "</body>\n"
            "</html>\n";
        src = malloc(strlen(demo) + 1);
        if (!src) { perror("malloc"); return 1; }
        strcpy(src, demo);
    }

    Node *doc = parse_document(src);
    print_tree(doc, 0);

    free_tree(doc);
    free(src);
    return 0;
}
