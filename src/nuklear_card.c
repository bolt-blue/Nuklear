#include "nuklear.h"
#include "nuklear_internal.h"

/* ==========================================================================
 *
 *                                   CARD
 *
 * ========================================================================== */

/*
 * nk_card_xxx _heavily_ borrows from nk_group_xxx.  It is therefore based on
 * nk_window, but simplified.
 *
 * NOTE:
 * - no scrollbars: all card id's are the same; if I understand correctly,
 *   this would wreak havoc if scrollbars were used.
 * - always dynamic: allows us to set the panel background color so that it
 *   may be inherited by e.g. calls to nk_group_begin that the caller may
 *   place within the card.
 *
 * TODO: Enable rounded corners
 *  - Might require height to be set
 *  - Would have to call nk_draw_rect ourselves
 *  - Would remove the requirement of NK_WINDOW_DYNAMIC to set the panel color
 */
NK_API nk_bool
nk_card_begin(struct nk_context *ctx, float height, const struct nk_style_card *style)
{
    struct nk_rect bounds;
    struct nk_window panel;
    struct nk_window *win;
    int id_len;
    nk_hash id_hash;
    nk_flags flags = NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_DYNAMIC;
    nk_uint *x_offset;
    nk_uint *y_offset;

    // If padding is insufficient to balance rounding, drawing overlap may
    // occur, depending on what the caller places inside the card.
    // From visual inspection, padding can actually be a little less than half
    // the value of rounding, but I've not yet determined a reasonable formula
    // so have opted for something simple for now.
    NK_ASSERT(style->padding.x * 2 >= style->rounding
        && style->padding.y * 2 >= style->rounding);

    float total_height = height + style->margin.y * 2;
    nk_row_layout(ctx, NK_DYNAMIC, total_height, 1, 0);

    win = ctx->current;
    nk_panel_alloc_space(&bounds, ctx);

    {const struct nk_rect *c = &win->layout->clip;
    if (!NK_INTERSECT(c->x, c->y, c->w, c->h, bounds.x, bounds.y, bounds.w, bounds.h)) {
        return 0;
    }}
    if (win->flags & NK_WINDOW_ROM)
        flags |= NK_WINDOW_ROM;

    bounds.x += style->margin.x;
    bounds.y += style->margin.y;
    bounds.w -= style->margin.x * 2;
    bounds.h -= style->margin.y * 2;

    // TODO: Choose color based on normal, hover or active
    struct nk_command_buffer *out = &win->buffer;
    struct nk_color color = style->normal.data.color;
    nk_fill_rect(out, bounds, style->rounding, color);
    // Also set the dynamic window background. Without this, the caller would
    // have to manually set it if they place e.g. a nk_group_begin inside
    // the card.
    // This assumed the card color is different from the existing window
    // background.
    // TODO: Only update window.background if a style color has been set
    ctx->style.window.background = style->normal.data.color;

    bounds.x += style->padding.x;
    bounds.y += style->padding.y;
    bounds.w -= style->padding.x * 2;
    bounds.h -= style->padding.y * 2;

    // WARNING: Reusing the same id for every instance of an nk_card. This is
    // possibly a terrible idea, but appears to work as long as we don't try
    // to support scrollbars (and anything else?).
    // TODO: What alternative(s) might we have?
    const char *id = "card";
    id_len = (int)nk_strlen(id);
    id_hash = nk_murmur_hash(id, (int)id_len, NK_PANEL_GROUP);
    x_offset = nk_find_value(win, id_hash);
    if (!x_offset) {
        x_offset = nk_add_value(ctx, win, id_hash, 0);
        y_offset = nk_add_value(ctx, win, id_hash+1, 0);

        NK_ASSERT(x_offset);
        NK_ASSERT(y_offset);
        if (!x_offset || !y_offset) return 0;
        *x_offset = *y_offset = 0;
    } else y_offset = nk_find_value(win, id_hash+1);

    /* initialize a fake window to create the panel from */
    nk_zero(&panel, sizeof(panel));
    panel.bounds = bounds;
    panel.flags = flags;
    panel.buffer = win->buffer;
    panel.layout = (struct nk_panel*)nk_create_panel(ctx);
    ctx->current = &panel;
    nk_panel_begin(ctx, 0, NK_PANEL_GROUP);

    win->buffer = panel.buffer;
    win->buffer.clip = panel.layout->clip;
    panel.layout->offset_x = x_offset;
    panel.layout->offset_y = y_offset;
    panel.layout->parent = win->layout;
    win->layout = panel.layout;

    ctx->current = win;
    if ((panel.layout->flags & NK_WINDOW_CLOSED) ||
        (panel.layout->flags & NK_WINDOW_MINIMIZED))
    {
        nk_flags f = panel.layout->flags;
        nk_group_scrolled_end(ctx);
        if (f & NK_WINDOW_CLOSED)
            return NK_WINDOW_CLOSED;
        if (f & NK_WINDOW_MINIMIZED)
            return NK_WINDOW_MINIMIZED;
    }

    return 1;
}

NK_API void
nk_card_end(struct nk_context *ctx)
{
    struct nk_window *win;
    struct nk_panel *parent;
    struct nk_panel *g;

    struct nk_rect clip;
    struct nk_window pan;
    struct nk_vec2 panel_padding;

    NK_ASSERT(ctx);
    NK_ASSERT(ctx->current);
    if (!ctx || !ctx->current)
        return;

    /* make sure nk_group_begin was called correctly */
    NK_ASSERT(ctx->current);
    win = ctx->current;
    NK_ASSERT(win->layout);
    g = win->layout;
    NK_ASSERT(g->parent);
    parent = g->parent;

    /* dummy window */
    nk_zero_struct(pan);
    panel_padding = nk_panel_get_padding(&ctx->style, NK_PANEL_GROUP);
    pan.bounds.y = g->bounds.y - (g->header_height + g->menu.h);
    pan.bounds.x = g->bounds.x - panel_padding.x;
    pan.bounds.w = g->bounds.w + 2 * panel_padding.x;
    pan.bounds.h = g->bounds.h + g->header_height + g->menu.h;
    if (g->flags & NK_WINDOW_BORDER) {
        pan.bounds.x -= g->border;
        pan.bounds.y -= g->border;
        pan.bounds.w += 2*g->border;
        pan.bounds.h += 2*g->border;
    }
    pan.scrollbar.x = *g->offset_x;
    pan.scrollbar.y = *g->offset_y;
    pan.flags = g->flags;
    pan.buffer = win->buffer;
    pan.layout = g;
    pan.parent = win;
    ctx->current = &pan;

    /* make sure group has correct clipping rectangle */
    nk_unify(&clip, &parent->clip, pan.bounds.x, pan.bounds.y,
        pan.bounds.x + pan.bounds.w, pan.bounds.y + pan.bounds.h + panel_padding.x);
    nk_push_scissor(&pan.buffer, clip);
    nk_end(ctx);

    win->buffer = pan.buffer;
    nk_push_scissor(&win->buffer, parent->clip);
    ctx->current = win;
    win->layout = parent;
    g->bounds = pan.bounds;
    return;
}
