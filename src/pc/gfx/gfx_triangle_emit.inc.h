/* Intentionally included twice for profiling A/B. Positions and UVs stay
 * shared; GFX_PREPARED_MATERIAL reuses normalized material constants.
 * Uses gfx_sp_tri1 locals; no include guard. */
#ifdef GFX_PREPARED_MATERIAL
    float prepared_lod = 0.0f;
    if (triangle_state.needs_lod) {
        float distance_frac = (v1->w - 3000.0f) / 3000.0f;
        if (distance_frac < 0.0f) distance_frac = 0.0f;
        if (distance_frac > 1.0f) distance_frac = 1.0f;
        // Preserve the original intermediate 8-bit quantization.
        uint8_t lod = distance_frac * 255.0f;
        prepared_lod = lod / 255.0f;
    }
#endif
    for (int i = 0; i < 3; i++) {
        float z = v_arr[i]->z, w = v_arr[i]->w;
        if (z_is_from_0_to_1) {
            z = (z + w) / 2.0f;
        }

        EMIT_FLOAT(v_arr[i]->x);
        EMIT_FLOAT(v_arr[i]->y);
#ifdef TARGET_N3DS
        EMIT_FLOAT(-z);
#else
        EMIT_FLOAT(z);
#endif
        EMIT_FLOAT(w);

        if (use_texture) {
            float u = (v_arr[i]->u - rdp.texture_tile.uls * 8) / 32.0f;
            float v = (v_arr[i]->v - rdp.texture_tile.ult * 8) / 32.0f;
            if ((rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT) {
                // Linear filter adds 0.5f to the coordinates
                u += 0.5f;
                v += 0.5f;
            }
            EMIT_FLOAT(u / tex_width);
            EMIT_FLOAT(v / tex_height);
#ifdef TARGET_WII_U
            // Padding for faster GPU reading
            EMIT_FLOAT(0.0f);
            EMIT_FLOAT(0.0f);
#endif
        }
#ifndef TARGET_N3DS
        if (use_fog) {
#ifdef GFX_PREPARED_MATERIAL
            EMIT_FLOAT(triangle_state.fog_rgb[0]);
            EMIT_FLOAT(triangle_state.fog_rgb[1]);
            EMIT_FLOAT(triangle_state.fog_rgb[2]);
#else
            EMIT_FLOAT(rdp.fog_color.r / 255.0f);
            EMIT_FLOAT(rdp.fog_color.g / 255.0f);
            EMIT_FLOAT(rdp.fog_color.b / 255.0f);
#endif
            EMIT_FLOAT(v_arr[i]->color.a / 255.0f); // fog factor (not alpha)
        }
#endif
        for (int j = 0; j < num_inputs; j++) {
#ifdef GFX_PREPARED_MATERIAL
            uint8_t rgb = triangle_state.rgb_kind[j];
            if (rgb == CC_SHADE) {
                EMIT_FLOAT(v_arr[i]->color.r / 255.0f);
                EMIT_FLOAT(v_arr[i]->color.g / 255.0f);
                EMIT_FLOAT(v_arr[i]->color.b / 255.0f);
            } else if (rgb == CC_LOD) {
                EMIT_FLOAT(prepared_lod);
                EMIT_FLOAT(prepared_lod);
                EMIT_FLOAT(prepared_lod);
            } else {
                EMIT_FLOAT(triangle_state.constant_rgb[j][0]);
                EMIT_FLOAT(triangle_state.constant_rgb[j][1]);
                EMIT_FLOAT(triangle_state.constant_rgb[j][2]);
            }
            if (use_alpha) {
                uint8_t alpha = triangle_state.alpha_kind[j];
                if (alpha == CC_SHADE) {
                    EMIT_FLOAT(use_fog ? 1.0f : v_arr[i]->color.a / 255.0f);
                } else if (alpha == CC_LOD) {
                    EMIT_FLOAT(prepared_lod);
                } else {
                    EMIT_FLOAT(triangle_state.constant_alpha[j]);
                }
            } else {
                EMIT_FLOAT(1.0f);
            }
#else
            struct RGBA *color;
            struct RGBA tmp;
            for (int k = 0; k < 1 + (use_alpha ? 1 : 0); k++) {
                switch (comb->shader_input_mapping[k][j]) {
                    case CC_PRIM:
                        color = &rdp.prim_color;
                        break;
                    case CC_SHADE:
                        color = &v_arr[i]->color;
                        break;
                    case CC_ENV:
                        color = &rdp.env_color;
                        break;
                    case CC_LOD:
                    {
                        float distance_frac = (v1->w - 3000.0f) / 3000.0f;
                        if (distance_frac < 0.0f) distance_frac = 0.0f;
                        if (distance_frac > 1.0f) distance_frac = 1.0f;
                        tmp.r = tmp.g = tmp.b = tmp.a = distance_frac * 255.0f;
                        color = &tmp;
                        break;
                    }
                    default:
                        memset(&tmp, 0, sizeof(tmp));
                        color = &tmp;
                        break;
                }
                if (k == 0) {
                    EMIT_FLOAT(color->r / 255.0f);
                    EMIT_FLOAT(color->g / 255.0f);
                    EMIT_FLOAT(color->b / 255.0f);
#ifdef TARGET_WII_U
                    if (!use_alpha) {
                        EMIT_FLOAT(1.0f);
                    }
#endif
                } else {
                    if (use_fog && color == &v_arr[i]->color) {
                        // Shade alpha is 100% for fog
                        EMIT_FLOAT(1.0f);
                    } else {
                        EMIT_FLOAT(color->a / 255.0f);
                    }
                }
            }
#endif
        }
    }
