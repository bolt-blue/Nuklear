#include "nuklear.h"
#include "nuklear_internal.h"

/* ==========================================================================
 *
 *                                   CARD
 *
 * ========================================================================== */

NK_LIB const struct nk_style_item*
nk_draw_card(struct nk_command_buffer *out,
    const struct nk_rect *bounds, nk_flags state,
    const struct nk_style_card *style, struct nk_color *window_background)
{
    const struct nk_style_item *background;
    if (state & NK_WIDGET_STATE_HOVER)
        background = &style->hover;
    else if (state & NK_WIDGET_STATE_ACTIVED)
        background = &style->active;
    else background = &style->normal;

    struct nk_color color = nk_rgb_factor(background->data.color, style->color_factor_background);

    // Set the dynamic window background. Without this, the caller would
    // have to manually set it if they place e.g. a nk_group_begin inside
    // the card.
    // This assumes the card color is different from the existing window
    // background.
    if (window_background)
        *window_background = color;

    nk_fill_rect(out, *bounds, style->rounding, color);
    nk_stroke_rect(out, *bounds, style->rounding, style->border,
        nk_rgb_factor(style->border_color, style->color_factor_background));

    return background;
}
NK_LIB nk_bool
nk_card_behavior(nk_flags *state, struct nk_rect r, const struct nk_input *i)
{
    int ret = 0;
    nk_widget_state_reset(state);
    if (!i) return 0;
    if (nk_input_is_mouse_hovering_rect(i, r)) {
        *state = NK_WIDGET_STATE_HOVERED;
        if (nk_input_is_mouse_down(i, NK_BUTTON_LEFT))
            *state = NK_WIDGET_STATE_ACTIVE;
        if (nk_input_has_mouse_click_in_button_rect(i, NK_BUTTON_LEFT, r)) {
            ret =
#ifdef NK_BUTTON_TRIGGER_ON_RELEASE
                nk_input_is_mouse_released(i, NK_BUTTON_LEFT);
#else
                nk_input_is_mouse_pressed(i, NK_BUTTON_LEFT);
#endif
        }
    }
    if (*state & NK_WIDGET_STATE_HOVER && !nk_input_is_mouse_prev_hovering_rect(i, r))
        *state |= NK_WIDGET_STATE_ENTERED;
    else if (nk_input_is_mouse_prev_hovering_rect(i, r))
        *state |= NK_WIDGET_STATE_LEFT;
    return ret;
}

NK_LIB nk_bool
nk_do_card(nk_flags *state, struct nk_command_buffer *out, struct nk_rect bounds,
    const struct nk_style_card *style, const struct nk_input *in,
    struct nk_color *window_background, struct nk_style_text *text)
{
    struct nk_rect touch_bounds;
    nk_bool ret = nk_false;

    NK_ASSERT(state);
    NK_ASSERT(style);
    NK_ASSERT(out);
    if (!out || !style)
        return nk_false;

    /* execute button behavior */
    touch_bounds.x = bounds.x - style->touch_padding.x;
    touch_bounds.y = bounds.y - style->touch_padding.y;
    touch_bounds.w = bounds.w + 2 * style->touch_padding.x;
    touch_bounds.h = bounds.h + 2 * style->touch_padding.y;

    ret = nk_card_behavior(state, touch_bounds, in);

    const struct nk_style_item *background;
    background = nk_draw_card(out, &bounds, *state, style, window_background);

    // TODO: Remove background from above call?
    (void)background;

    /* set text color */
    if (text) {
        if (*state & NK_WIDGET_STATE_HOVER)
            text->color = style->text_hover;
        else if (*state & NK_WIDGET_STATE_ACTIVED)
            text->color = style->text_active;
        else text->color = style->text_normal;

        text->color = nk_rgb_factor(text->color, style->color_factor_text);
    }

    return ret;
}

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
nk_card_begin(struct nk_context *ctx, float height, const struct nk_style_card *style,
    struct nk_style_text *text_style, nk_bool *pressed_state)
{
    struct nk_rect bounds;
    struct nk_rect content;
    struct nk_window panel;

    struct nk_window *win;
    struct nk_panel *layout;
    struct nk_command_buffer *out;
    const struct nk_input *in;

    enum nk_widget_layout_states state;

    int id_len;
    nk_hash id_hash;
    nk_flags flags = NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_DYNAMIC;
    nk_uint *x_offset;
    nk_uint *y_offset;

    NK_ASSERT(ctx);
    NK_ASSERT(ctx->current);
    NK_ASSERT(ctx->current->layout);
    NK_ASSERT(style);
    // If padding is insufficient to balance rounding, drawing overlap may
    // occur, depending on what the caller places inside the card.
    // From visual inspection, padding can actually be a little less than half
    // the value of rounding, but I've not yet determined a reasonable formula
    // so have opted for something simple for now.
    NK_ASSERT(style->padding.x * 2 >= style->rounding
        && style->padding.y * 2 >= style->rounding);
    if (!ctx || !ctx->current || !ctx->current->layout || !style) return 0;

    win = ctx->current;
    out = &win->buffer;

    float total_height = height + style->margin.y * 2;
    nk_row_layout(ctx, NK_DYNAMIC, total_height, 1, 0);

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

    nk_panel_alloc_space(&bounds, ctx);

    {const struct nk_rect *c = &win->layout->clip;
    if (!NK_INTERSECT(c->x, c->y, c->w, c->h, bounds.x, bounds.y, bounds.w, bounds.h)) {
        return 0;
    }}
    if (win->flags & NK_WINDOW_ROM)
        flags |= NK_WINDOW_ROM;

    // TODO: Remove margin option? It seems out of keeping with the lib.
    bounds.x += style->margin.x;
    bounds.y += style->margin.y;
    bounds.w -= style->margin.x * 2;
    bounds.h -= style->margin.y * 2;

    // TODO: Do we need this and it's use in the following check?
    layout = win->layout;

    in = (flags & NK_WINDOW_ROM || layout->flags & NK_WINDOW_ROM)
        ? 0 : &ctx->input;

    /* determine if the card has been pressed */
    *pressed_state = nk_do_card(&ctx->last_widget_state, out, bounds, style,
        in, &ctx->style.window.background, text_style);

    /* calculate card content space */
    // TODO: Make it so the additions may be uncommented
    content.x = bounds.x + style->padding.x;// + style->border + style->rounding;
    content.y = bounds.y + style->padding.y;// + style->border + style->rounding;
    content.w = bounds.w - 2 * (style->padding.x);// + style->border + style->rounding);
    content.h = bounds.h - 2 * (style->padding.y);// + style->border + style->rounding);

    /* initialize a fake window to create the panel from */
    nk_zero(&panel, sizeof(panel));
    panel.bounds = content;
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
