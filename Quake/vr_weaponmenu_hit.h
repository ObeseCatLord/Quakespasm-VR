#ifndef VR_WEAPONMENU_HIT_H
#define VR_WEAPONMENU_HIT_H

/* Tests an aim ray against the complete footprint of a 3D text label.  The
 * label origin is its left edge and vertical midpoint, matching
 * VR_DrawText3DAligned. */
static int VR_WeaponMenuRayHitsLabelRect(const float ray_origin[3],
                                         const float ray_direction[3],
                                         const float plane_normal[3],
                                         const float label_origin[3],
                                         const float label_right[3],
                                         const float label_up[3],
                                         float label_width,
                                         float label_height,
                                         float outline_padding,
                                         float hit_point[3]) {
  float ray_to_label[3];
  float denominator;
  float distance;
  float local[3];
  float local_right;
  float local_up;

  ray_to_label[0] = label_origin[0] - ray_origin[0];
  ray_to_label[1] = label_origin[1] - ray_origin[1];
  ray_to_label[2] = label_origin[2] - ray_origin[2];
  denominator = ray_direction[0] * plane_normal[0] +
                ray_direction[1] * plane_normal[1] +
                ray_direction[2] * plane_normal[2];
  if (denominator > -0.001f && denominator < 0.001f)
    return 0;

  distance = (ray_to_label[0] * plane_normal[0] +
              ray_to_label[1] * plane_normal[1] +
              ray_to_label[2] * plane_normal[2]) /
             denominator;
  if (distance <= 0.0f)
    return 0;

  hit_point[0] = ray_origin[0] + distance * ray_direction[0];
  hit_point[1] = ray_origin[1] + distance * ray_direction[1];
  hit_point[2] = ray_origin[2] + distance * ray_direction[2];
  local[0] = hit_point[0] - label_origin[0];
  local[1] = hit_point[1] - label_origin[1];
  local[2] = hit_point[2] - label_origin[2];
  local_right = local[0] * label_right[0] + local[1] * label_right[1] +
                local[2] * label_right[2];
  local_up = local[0] * label_up[0] + local[1] * label_up[1] +
             local[2] * label_up[2];

  return local_right >= -outline_padding &&
         local_right <= label_width + outline_padding &&
         local_up >= -label_height * 0.5f - outline_padding &&
         local_up <= label_height * 0.5f + outline_padding;
}

#endif
