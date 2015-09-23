#include "z-doc.h"

#include "angband.h"
#include "c-string.h"

#include <assert.h>

doc_pos_t doc_pos_create(int x, int y)
{
    doc_pos_t result;
    result.x = x;
    result.y = y;
    return result;
}

doc_pos_t doc_pos_invalid(void)
{
    return doc_pos_create(-1, -1);
}

bool doc_pos_is_valid(doc_pos_t pos)
{
    if (pos.x >= 0 && pos.y >= 0)
        return TRUE;
    return FALSE;
}

int doc_pos_compare(doc_pos_t left, doc_pos_t right)
{
    if (left.y < right.y)
        return -1;
    if (left.y > right.y)
        return 1;
    if (left.x < right.x)
        return -1;
    if (left.x > right.x)
        return 1;
    return 0;
}

doc_region_t doc_region_create(int x1, int y1, int x2, int y2)
{
    doc_region_t result;
    result.start.x = x1;
    result.start.y = y1;
    result.stop.x = x2;
    result.stop.y = y2;
    return result;
}

doc_region_t doc_region_invalid(void)
{
    doc_region_t result;
    result.start = doc_pos_invalid();
    result.stop = doc_pos_invalid();
    return result;
}

bool doc_region_is_valid(doc_region_ptr region)
{
    if (region)
    {
        if ( doc_pos_is_valid(region->start)
          && doc_pos_is_valid(region->stop)
          && doc_pos_compare(region->start, region->stop) <= 0 )
        {
            return TRUE;
        }
    }
    return FALSE;
}

bool doc_region_contains(doc_region_ptr region, doc_pos_t pos)
{
    if (doc_region_is_valid(region) && doc_pos_is_valid(pos))
    {
        if ( doc_pos_compare(region->start, pos) <= 0
          && doc_pos_compare(region->stop, pos) > 0 )
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void _doc_bookmark_free(vptr pv)
{
    doc_bookmark_ptr mark = pv;
    string_free(mark->name);
    free(mark);
}

static void _doc_link_free(vptr pv)
{
    doc_link_ptr link = pv;
    string_free(link->file);
    string_free(link->topic);
    free(link);
}

#define PAGE_HEIGHT 128
#define PAGE_NUM(a) ((a)>>7)
#define PAGE_OFFSET(a) ((a)%128)

doc_ptr doc_alloc(int width)
{
    doc_ptr res = malloc(sizeof(doc_t));
    doc_style_ptr style;

    res->cursor.x = 0;
    res->cursor.y = 0;
    res->selection = doc_region_invalid();
    res->width = width;
    res->pages = vec_alloc(free);
    res->styles = str_map_alloc(free);
    res->bookmarks = vec_alloc(_doc_bookmark_free);
    res->links = int_map_alloc(_doc_link_free);

    /* Default Styles:
       We currently lack a way to define styles inside a document.
       Perhaps these should be globally available as well, an initialized
       from a preference file for the entire document system. */
    style = doc_style(res, "normal");
    style->right = MIN(72, width);

    style = doc_style(res, "note");
    style->color = TERM_L_GREEN;
    style->left = 4;
    style->right = MIN(60, width);

    style = doc_style(res, "title");
    style->color = TERM_L_BLUE;

    style = doc_style(res, "heading");
    style->color = TERM_RED;

    style = doc_style(res, "keyword");
    style->right = MIN(72, width);
    style->color = TERM_L_RED;

    style = doc_style(res, "keypress");
    style->right = MIN(72, width);
    style->color = TERM_ORANGE;

    style = doc_style(res, "link");
    style->right = MIN(72, width);
    style->color = TERM_L_GREEN;

    style = doc_style(res, "screenshot");
    style->options |= DOC_STYLE_NO_WORDWRAP;

    style = doc_style(res, "selection");
    style->color = TERM_YELLOW;

    doc_change_style(res, "normal");
    return res;
}

void doc_free(doc_ptr doc)
{
    if (doc)
    {
        vec_free(doc->pages);
        str_map_free(doc->styles);
        vec_free(doc->bookmarks);
        int_map_free(doc->links);

        free(doc);
    }
}

doc_pos_t doc_cursor(doc_ptr doc)
{
    return doc->cursor;
}

doc_region_t doc_all(doc_ptr doc)
{
    doc_region_t result;
    result.start = doc_pos_create(0, 0);
    result.stop = doc->cursor;
    return result;
}

doc_pos_t doc_next_bookmark(doc_ptr doc, doc_pos_t pos)
{
    int i;

    for (i = 0; i < vec_length(doc->bookmarks); i++)
    {
        doc_bookmark_ptr mark = vec_get(doc->bookmarks, i);

        if (doc_pos_compare(pos, mark->pos) < 0)
            return mark->pos;
    }
    return doc_pos_invalid();
}

doc_pos_t doc_prev_bookmark(doc_ptr doc, doc_pos_t pos)
{
    int i;
    doc_bookmark_ptr last = NULL;

    for (i = 0; i < vec_length(doc->bookmarks); i++)
    {
        doc_bookmark_ptr mark = vec_get(doc->bookmarks, i);

        if (doc_pos_compare(mark->pos, pos) < 0)
            last = mark;
        else
            break;
    }
    if (last)
        return last->pos;
    return doc_pos_invalid();
}

doc_pos_t doc_find_bookmark(doc_ptr doc, cptr name)
{
    int i;

    for (i = 0; i < vec_length(doc->bookmarks); i++)
    {
        doc_bookmark_ptr mark = vec_get(doc->bookmarks, i);

        if (strcmp(name, string_buffer(mark->name)) == 0)
            return mark->pos;
    }
    return doc_pos_invalid();
}

static bool _row_test_str(doc_char_ptr cell, int ncell, cptr what, int nwhat)
{
    int i;

    if (ncell < nwhat)
        return FALSE;

    for (i = 0; i < nwhat; i++, cell++)
    {
        char c = cell->c ? cell->c : ' ';
        assert(i < ncell);
        if (c != what[i]) break;
    }

    if (i == nwhat)
        return TRUE;

    return FALSE;
}

static int _row_find_str(doc_char_ptr cell, int ncell, cptr what)
{
    int i;
    int nwhat = strlen(what);

    for (i = 0; i < ncell - nwhat; i++)
    {
        if (_row_test_str(cell + i, ncell - i, what, nwhat))
            return i;
    }

    return -1;
}

doc_pos_t doc_find_next(doc_ptr doc, cptr text, doc_pos_t start)
{
    int y;

    for (y = start.y; y <= doc->cursor.y; y++)
    {
        int          x = (y == start.y) ? start.x : 0;
        int          ncell = doc->width - x;
        doc_char_ptr cell = doc_char(doc, doc_pos_create(x, y));
        int          i = _row_find_str(cell, ncell, text);

        if (i >= 0)
        {
            doc->selection.start.x = x + i;
            doc->selection.start.y = y;

            doc->selection.stop.x = x + i + strlen(text);
            doc->selection.stop.y = y;

            return doc->selection.start;
        }
   }

    doc->selection = doc_region_invalid();
    return doc_pos_invalid();
}

doc_pos_t doc_find_prev(doc_ptr doc, cptr text, doc_pos_t start)
{
    int y;

    for (y = start.y; y >= 0; y--)
    {
        int          ncell = (y == start.y) ? start.x - 1 : doc->width;
        doc_char_ptr cell = doc_char(doc, doc_pos_create(0, y));
        int          i = _row_find_str(cell, ncell, text);

        if (i >= 0)
        {
            doc->selection.start.x = i;
            doc->selection.start.y = y;

            doc->selection.stop.x = i + strlen(text);
            doc->selection.stop.y = y;

            return doc->selection.start;
        }
   }

    doc->selection = doc_region_invalid();
    return doc_pos_invalid();
}

doc_style_ptr doc_style(doc_ptr doc, cptr name)
{
    doc_style_ptr result = str_map_find(doc->styles, name);
    if (!result)
    {
        result = malloc(sizeof(doc_style_t));
        result->color = TERM_WHITE;
        result->left = 0;
        result->right = doc->width;
        result->options = 0;
        str_map_add(doc->styles, name, result);
    }
    return result;
}

doc_pos_t doc_newline(doc_ptr doc)
{
    doc->cursor.y++;
    doc->cursor.x = doc->current_style.left;
    return doc->cursor;
}

cptr doc_parse_tag(cptr pos, doc_tag_ptr tag)
{
    /* prepare to fail! */
    tag->type = DOC_TAG_NONE;
    tag->arg = NULL;
    tag->arg_size = 0;

    /* <name:arg> where name in {"color", "style", "topic", "link"} */
    if (*pos == '<')
    {
        doc_tag_t result = {0};
        cptr seek = pos + 1;
        char name[MAX_NLEN];
        int  ct = 0;
        for (;;)
        {
            if (!*seek || strchr(" <>\r\n\t", *seek)) return pos;
            if (*seek == ':') break;
            name[ct++] = *seek;
            if (ct >= MAX_NLEN) return pos;
            seek++;
        }
        name[ct] = '\0';

        /* [pos,seek) is the name of the tag */
        if (strcmp(name, "color") == 0)
            result.type = DOC_TAG_COLOR;
        else if (strcmp(name, "style") == 0)
            result.type = DOC_TAG_STYLE;
        else if (strcmp(name, "topic") == 0)
            result.type = DOC_TAG_TOPIC;
        else if (strcmp(name, "link") == 0)
            result.type = DOC_TAG_LINK;
        else if (strcmp(name, "$") == 0 || strcmp(name, "var") == 0)
            result.type = DOC_TAG_VAR;
        else
            return pos;

        assert(*seek == ':');
        seek++;
        result.arg = seek;

        ct = 0;
        for (;;)
        {
            if (!*seek || strchr(" <\r\n\t", *seek)) return pos;
            if (*seek == '>') break;
            ct++;
            seek++;
        }
        result.arg_size = ct;

        assert(*seek == '>');
        seek++;

        if (!result.arg_size)
            return pos;
        else if (result.type == DOC_TAG_COLOR)
        {
            if ( result.arg_size == 1
              && !strchr("dwsorgbuDWvyRGBU*", result.arg[0]) )
            {
                return pos;
            }
        }

        *tag = result;
        return seek;
    }

    return pos;
}

cptr doc_lex(cptr pos, doc_token_ptr token)
{
    if (!*pos)
    {
        token->type = DOC_TOKEN_EOF;
        token->pos = pos;
        token->size = 0;
        return pos;
    }
    if (*pos == '\r')
        pos++;

    if (*pos == '\n')
    {
        token->type = DOC_TOKEN_NEWLINE;
        token->pos = pos++;
        token->size = 1;
        return pos;
    }
    if (*pos == ' ' || *pos == '\t')
    {
        token->type = DOC_TOKEN_WHITESPACE;
        token->pos = pos++;
        token->size = 1;
        while (*pos && (*pos == ' ' || *pos == '\t'))
        {
            token->size++;
            pos++;
        }
        return pos;
    }
    if (*pos == '<')
    {
        token->type = DOC_TOKEN_TAG;
        token->pos = pos;
        token->size = 0;
        pos = doc_parse_tag(pos, &token->tag);
        if (token->tag.type != DOC_TAG_NONE)
        {
            token->size = pos - token->pos;
            return pos;
        }
    }
    token->type = DOC_TOKEN_WORD;
    token->pos = pos++;
    token->size = 1;

    while (*pos && !strchr(" <\n", *pos))
    {
        token->size++;
        pos++;
    }
    return pos;
}

static void _doc_process_var(doc_ptr doc, cptr name)
{
    if (strcmp(name, "version") == 0)
    {
        string_ptr s = string_alloc(NULL);
        string_printf(s, "%d.%d.%d.", VER_MAJOR, VER_MINOR, VER_PATCH);
        doc_insert(doc, string_buffer(s));
        string_free(s);
    }
}

void doc_change_style(doc_ptr doc, cptr name)
{
    doc_style_ptr style = doc_style(doc, name);
    if (style)
    {
        doc->current_style = *style;
        doc->current_color = doc->current_style.color;
        if (doc->cursor.x < doc->current_style.left)
            doc->cursor.x = doc->current_style.left;
    }
}

static void _doc_process_tag(doc_ptr doc, doc_tag_ptr tag)
{
    if (tag->type == DOC_TAG_COLOR)
    {
        assert(tag->arg);
        if (tag->arg_size == 1)
        {
            switch (tag->arg[0])
            {
            case '*': doc->current_color = doc->current_style.color; break;
            case 'd': doc->current_color = TERM_DARK; break;
            case 'w': doc->current_color = TERM_WHITE; break;
            case 's': doc->current_color = TERM_SLATE; break;
            case 'o': doc->current_color = TERM_ORANGE; break;
            case 'r': doc->current_color = TERM_RED; break;
            case 'g': doc->current_color = TERM_GREEN; break;
            case 'b': doc->current_color = TERM_BLUE; break;
            case 'u': doc->current_color = TERM_UMBER; break;
            case 'D': doc->current_color = TERM_L_DARK; break;
            case 'W': doc->current_color = TERM_L_WHITE; break;
            case 'v': doc->current_color = TERM_VIOLET; break;
            case 'y': doc->current_color = TERM_YELLOW; break;
            case 'R': doc->current_color = TERM_L_RED; break;
            case 'G': doc->current_color = TERM_L_GREEN; break;
            case 'B': doc->current_color = TERM_L_BLUE; break;
            case 'U': doc->current_color = TERM_L_UMBER; break;
            }
        }
        else
        {
            string_ptr arg = string_nalloc(tag->arg, tag->arg_size);
            doc_style_ptr style = str_map_find(doc->styles, string_buffer(arg));
            if (style)
                doc->current_color = style->color;
            string_free(arg);
        }
    }
    else
    {
        string_ptr arg = string_nalloc(tag->arg, tag->arg_size);

        switch (tag->type)
        {
        case DOC_TAG_STYLE:
            doc_change_style(doc, string_buffer(arg));
            break;
        case DOC_TAG_VAR:
            _doc_process_var(doc, string_buffer(arg));
            break;
        case DOC_TAG_TOPIC:
        {
            doc_bookmark_ptr mark = malloc(sizeof(doc_bookmark_t));
            mark->name = arg; /* steal ownership */
            arg = NULL;
            mark->pos = doc->cursor;
            vec_add(doc->bookmarks, mark);
            break;
        }
        case DOC_TAG_LINK:
        {
            doc_link_ptr link = malloc(sizeof(doc_link_t));
            int          split = string_chr(arg, '#');
            int          ch = 'a' + int_map_count(doc->links);

            if (split >= 0)
            {
                substring_t left = string_left(arg, split);
                substring_t right = string_right(arg, string_length(arg) - split - 1);

                link->file = string_ssalloc(&left);
                link->topic = string_ssalloc(&right);
            }
            else
            {
                link->file = arg; /* steal ownership */
                arg = NULL;
                link->topic = NULL;
            }
            int_map_add(doc->links, ch, link);
            {
                string_ptr s = string_alloc(NULL);
                string_printf(s, "<style:link>[%c]<style:normal>", ch);
                doc_insert(doc, string_buffer(s));
                string_free(s);
            }
            break;
        }
        }

        string_free(arg);
    }
}

doc_pos_t doc_insert(doc_ptr doc, cptr text)
{
    doc_token_t token;
    doc_token_t queue[10];
    int         qidx = 0;
    cptr        pos = text;
    int         i, j, cb;

    for (;;)
    {
        pos = doc_lex(pos, &token);

        if (token.type == DOC_TOKEN_EOF)
            break;

        assert(pos != token.pos);

        if (token.type == DOC_TOKEN_TAG)
        {
            _doc_process_tag(doc, &token.tag);
            continue;
        }

        if (token.type == DOC_TOKEN_NEWLINE)
        {
            doc_newline(doc);
            continue;
        }

        if (token.type == DOC_TOKEN_WHITESPACE)
        {
            doc->cursor.x += token.size;
            continue;
        }

        assert(token.type == DOC_TOKEN_WORD);
        assert(token.size > 0);

        /* Queue Complexity is for "<color:R>difficult<color:*>!" which is actually a bit common! */
        qidx = 0;
        cb = token.size;
        queue[qidx++] = token;
        while (qidx < 10)
        {
            cptr peek = doc_lex(pos, &token);
            if (token.type == DOC_TOKEN_WORD)
            {
                cb += token.size;
            }
            else if (token.type == DOC_TOKEN_TAG && token.tag.type == DOC_TAG_COLOR)
            {
            }
            else
            {
                break;
            }
            queue[qidx++] = token;
            pos = peek;
        }

        if ( doc->cursor.x + cb >= doc->current_style.right
          && !(doc->current_style.options & DOC_STYLE_NO_WORDWRAP) )
        {
            doc_newline(doc);
        }

        for (i = 0; i < qidx; i++)
        {
            doc_token_ptr current = &queue[i];
            doc_char_ptr  cell = doc_char(doc, doc->cursor);

            assert(cell);
            if (current->type == DOC_TOKEN_TAG)
            {
                assert(current->tag.type == DOC_TAG_COLOR);
                _doc_process_tag(doc, &current->tag);
                continue;
            }
            assert(current->type == DOC_TOKEN_WORD);
            for (j = 0; j < current->size; j++)
            {
                cell->a = doc->current_color;
                cell->c = current->pos[j];
                if (cell->c == '\t')
                    cell->c = ' ';
                doc->cursor.x++;
                if (doc->cursor.x >= doc->width)
                {
                    if (doc->current_style.options & DOC_STYLE_NO_WORDWRAP)
                    {
                        i = qidx;
                        break;
                    }
                    doc_newline(doc);
                    cell = doc_char(doc, doc->cursor);
                }
                else
                    cell++;
            }
        }
    }
    return doc->cursor;
}

doc_char_ptr doc_char(doc_ptr doc, doc_pos_t pos)
{
    int            cb = doc->width * PAGE_HEIGHT * sizeof(doc_char_t);
    int            page_num = PAGE_NUM(pos.y);
    int            offset = PAGE_OFFSET(pos.y);
    doc_char_ptr   page = NULL;

    while (page_num >= vec_length(doc->pages))
    {
        page = malloc(cb);
        memset(page, 0, cb);
        vec_add(doc->pages, page);
    }

    page = vec_get(doc->pages, page_num);
    assert(offset * doc->width + pos.x < cb);
    return page + offset * doc->width + pos.x;
}

doc_pos_t doc_read_file(doc_ptr doc, FILE *fp)
{
    string_ptr s = string_alloc(NULL);

    while (!feof(fp))
    {
        string_fgets(s, fp);
        doc_insert(doc, string_buffer(s));
        doc_newline(doc);
    }

    string_free(s);
    return doc->cursor;
}

void doc_write_file(doc_ptr doc, FILE *fp)
{
    doc_pos_t    pos;
    doc_char_ptr cell;

    for (pos.y = 0; pos.y <= doc->cursor.y; pos.y++)
    {
        int cx = doc->width;
        pos.x = 0;
        if (pos.y == doc->cursor.y)
            cx = doc->cursor.x;
        cell = doc_char(doc, pos);
        for (; pos.x < cx; pos.x++)
        {
            if (cell->c)
                fputc(cell->c, fp);
            else
                fputc(' ', fp);
            cell++;
        }
        fputc('\n', fp);
   }
}

doc_pos_t doc_pos_next(doc_ptr doc, doc_pos_t pos)
{
    doc_pos_t result = pos;
    result.x++;
    if (result.x >= doc->width)
    {
        result.x = 0;
        result.y++;
    }
    return result;
}

#define _INVALID_COLOR 255
void doc_sync_term(doc_ptr doc, doc_region_t range, doc_pos_t term_pos)
{
    doc_pos_t pos;
    byte      selection_color = _INVALID_COLOR;

    assert(doc_region_is_valid(&range));

    if (doc_region_is_valid(&doc->selection))
    {
        doc_style_ptr style = str_map_find(doc->styles, "selection");
        if (style)
            selection_color = style->color;
    }

    for (pos = range.start; doc_pos_compare(pos, range.stop) < 0; pos = doc_pos_next(doc, pos))
    {
        int          term_x = term_pos.x + pos.x;
        int          term_y = term_pos.y + pos.y - range.start.y;
        char         c = ' ';
        byte         a = TERM_DARK;

        if (doc_pos_compare(pos, doc->cursor) < 0)
        {
            doc_char_ptr cell = doc_char(doc, pos);

            a = cell->a;
            if (cell->c)
                c = cell->c;

            if ( selection_color != _INVALID_COLOR
              && doc_region_contains(&doc->selection, pos) )
            {
                a = selection_color;
            }
        }
        Term_putch(term_x, term_y, a, c);
    }
}

#define _UNWIND 1
#define _OK 0
int doc_display(doc_ptr doc, cptr caption, int top)
{
    int     rc = _OK;
    char    finder_str[81];
    char    back_str[81];
    int     cx, cy;
    int     page_size;
    bool    done = FALSE;

    strcpy(finder_str, "");


    Term_get_size(&cx, &cy);
    page_size = cy - 4;

    if (top < 0)
        top = 0;
    if (top > doc->cursor.y - page_size)
        top = MAX(0, doc->cursor.y - page_size);

    Term_clear();
    while (!done)
    {
        int cmd;

        prt(format("[%s, Line %d/%d]", caption, top, doc->cursor.y), 0, 0);
        doc_sync_term(doc, doc_region_create(0, top, doc->width, top + page_size), doc_pos_create(0, 2));
        prt("[Press ESC to exit. Press ? for help]", cy - 1, 0);

        cmd = inkey_special(TRUE);

        if ('a' <= cmd && cmd <= 'z')
        {
            doc_link_ptr link = int_map_find(doc->links, cmd);
            if (link)
            {
                rc = doc_display_help(string_buffer(link->file), string_buffer(link->topic));
                if (rc == _UNWIND)
                    done = TRUE;
                continue;
            }
        }

        switch (cmd)
        {
        case '?':
            if (!strstr(caption, "helpinfo.txt"))
            {
                rc = doc_display_help("helpinfo.txt", NULL);
                if (rc == _UNWIND)
                    done = TRUE;
            }
            break;
        case ESCAPE:
            done = TRUE;
            break;
        case 'q':
            done = TRUE;
            rc = _UNWIND;
            break;
        case SKEY_TOP:
        case '7':
            top = 0;
            break;
        case SKEY_BOTTOM:
        case '1':
            top = MAX(0, doc->cursor.y - page_size);
            break;
        case SKEY_PGUP:
        case '9':
            top -= page_size;
            if (top < 0) top = 0;
            break;
        case SKEY_PGDOWN:
        case '3':
            top += page_size;
            if (top > doc->cursor.y - page_size)
                top = MAX(0, doc->cursor.y - page_size);
            break;
        case SKEY_UP:
        case '8':
            top--;
            if (top < 0) top = 0;
            break;
        case SKEY_DOWN:
        case '2':
            top++;
            if (top > doc->cursor.y - page_size)
                top = MAX(0, doc->cursor.y - page_size);
            break;
        case '>':
        {
            doc_pos_t pos = doc_next_bookmark(doc, doc_pos_create(0, top));
            if (doc_pos_is_valid(pos))
            {
                top = pos.y;
                if (top > doc->cursor.y - page_size)
                    top = MAX(0, doc->cursor.y - page_size);
            }
            break;
        }
        case '<':
        {
            doc_pos_t pos = doc_prev_bookmark(doc, doc_pos_create(0, top));
            if (doc_pos_is_valid(pos))
                top = pos.y;
            else
                top = 0;
            break;
        }
        case '|':
        {
            FILE *fp2;
            char buf[1024];
            char name[82];

            strcpy(name, "");

            if (!get_string("File name: ", name, 80)) break;
            path_build(buf, sizeof(buf), ANGBAND_DIR_USER, name);
            fp2 = my_fopen(buf, "w");
            if (!fp2)
            {
                msg_format("Failed to open file: %s", buf);
                break;
            }
            doc_write_file(doc, fp2);
            my_fclose(fp2);
            msg_format("Created file: %s", buf);
            msg_print(NULL);
            break;
        }
        case '/':
            prt("Find: ", cy - 1, 0);
            strcpy(back_str, finder_str);
            if (askfor(finder_str, 80))
            {
                if (finder_str[0])
                {
                    doc_pos_t pos = doc->selection.stop;
                    if (!doc_pos_is_valid(pos))
                        pos = doc_pos_create(0, top);
                    pos = doc_find_next(doc, finder_str, pos);
                    if (doc_pos_is_valid(pos))
                    {
                        top = pos.y;
                        if (top > doc->cursor.y - page_size)
                            top = MAX(0, doc->cursor.y - page_size);
                    }
                }
            }
            else strcpy(finder_str, back_str);
            break;
        case '\\':
            prt("Find: ", cy - 1, 0);
            strcpy(back_str, finder_str);
            if (askfor(finder_str, 80))
            {
                if (finder_str[0])
                {
                    doc_pos_t pos = doc->selection.start;
                    if (!doc_pos_is_valid(pos))
                        pos = doc_pos_create(doc->width, top + page_size);
                    pos = doc_find_prev(doc, finder_str, pos);
                    if (doc_pos_is_valid(pos))
                    {
                        top = pos.y;
                        if (top > doc->cursor.y - page_size)
                            top = MAX(0, doc->cursor.y - page_size);
                    }
                }
            }
            else strcpy(finder_str, back_str);
            break;
        }
    }
    return rc;
}

int doc_display_help(cptr file_name, cptr topic)
{
    int     rc = _OK;
    FILE   *fp = NULL;
    char    path[1024];
    char    caption[1024];
    doc_ptr doc = NULL;
    int     top = 0;

    sprintf(caption, "Help file '%s'", file_name);
    path_build(path, sizeof(path), ANGBAND_DIR_HELP, file_name);
    fp = my_fopen(path, "r");
    if (!fp)
    {
        cmsg_format(TERM_VIOLET, "Cannot open '%s'.", file_name);
        msg_print(NULL);
        return _OK;
    }

    doc = doc_alloc(80); /* Note: We include screen dumps of 80x27, so don't go below 80 */
    doc_read_file(doc, fp);

    if (topic)
    {
        doc_pos_t pos = doc_find_bookmark(doc, topic);
        if (doc_pos_is_valid(pos))
            top = pos.y;
    }

    rc = doc_display(doc, caption, top);
    doc_free(doc);
    return rc;
}