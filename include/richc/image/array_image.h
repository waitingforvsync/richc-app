/*
 * image/array_image.h - convenience include that generates rc_view_image,
 *                       rc_span_image, and rc_array_image.
 *
 * Include this header once to get:
 *   rc_view_image  { const rc_image *data; uint32_t num; }              read-only slice
 *   rc_span_image  {       rc_image *data; uint32_t num; }              mutable  slice
 *   rc_array_image {       rc_image *data; uint32_t num; uint32_t cap; } growable array
 */

#ifndef RC_IMAGE_ARRAY_IMAGE_H_
#define RC_IMAGE_ARRAY_IMAGE_H_

#include "richc/image/image.h"

#define ARRAY_T    rc_image
#define ARRAY_NAME rc_array_image
#define ARRAY_VIEW rc_view_image
#define ARRAY_SPAN rc_span_image
#include "richc/template/array.h"

#endif /* RC_IMAGE_ARRAY_IMAGE_H_ */
