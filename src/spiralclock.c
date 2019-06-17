#include <pebble.h>
#include <pebble-fctx/fctx.h>

static Window *s_main_window;
static Layer *s_draw_layer;

static TextLayer *s_date_month_layer;
static TextLayer *s_date_day_layer;

#if defined(PBL_COLOR)
static BitmapLayer *s_bgd_layer;
static GBitmap *s_bitmap_primary = NULL, *s_bitmap_secondary = NULL;
#endif

static int32_t minutes_ct = 0;
struct tm last_time;

char date_month_buf[3] = "??";
char date_day_buf[3] = "??";

#define TICK_COLOR PBL_IF_COLOR_ELSE(GColorLightGray , GColorWhite)
#define MINUTE_COVER_COLOR GColorBlack
#define HOUR_SPIRAL_COLOR GColorWhite

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);
  minutes_ct = (tick_time->tm_min + tick_time->tm_hour*60) % (12*60);
  last_time = *tick_time;
}

static void update_text(){
  strftime(date_month_buf, 3, "%m", &last_time);
  text_layer_set_text(s_date_month_layer, date_month_buf);

  strftime(date_day_buf, 3, "%d", &last_time);
  text_layer_set_text(s_date_day_layer, date_day_buf);
}

static void update_graphics(){
  layer_mark_dirty(s_draw_layer);
  #if defined(PBL_COLOR)
    if(last_time.tm_hour < 12){
      if(s_bitmap_secondary == NULL){
        gbitmap_destroy(s_bitmap_primary);
        s_bitmap_primary = NULL;
        s_bitmap_secondary = gbitmap_create_with_resource(RESOURCE_ID_BGD_SECONDARY);
        bitmap_layer_set_bitmap(s_bgd_layer, s_bitmap_secondary);
      }
    }else{
      if(s_bitmap_primary == NULL){
        gbitmap_destroy(s_bitmap_secondary);
        s_bitmap_secondary = NULL;
        s_bitmap_primary = gbitmap_create_with_resource(RESOURCE_ID_BGD_PRIMARY);
        bitmap_layer_set_bitmap(s_bgd_layer, s_bitmap_primary);
      }
    }
  #endif
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  update_graphics();
  update_text();
}

static FPoint get_angular_point(FPoint center, fixed_t r, int32_t angle) {
  return FPoint( center.x + (r * cos_lookup(angle)) / TRIG_MAX_RATIO,
                 center.y + (r * sin_lookup(angle)) / TRIG_MAX_RATIO);
}

static void make_arc(FContext *fctx, FPoint center, fixed_t start_r, fixed_t end_r, int32_t start_angle, int32_t end_angle){
  
  FPoint start_vec = FPoint( (start_r * cos_lookup(start_angle)) / TRIG_MAX_RATIO,
                             (start_r * sin_lookup(start_angle)) / TRIG_MAX_RATIO);
  FPoint end_vec = FPoint( (end_r * cos_lookup(end_angle)) / TRIG_MAX_RATIO,
                           (end_r * sin_lookup(end_angle)) / TRIG_MAX_RATIO);
  
  // CP_BASE should be 4/3 tan((end-start)/4)
  int32_t CP_ANG = (end_angle-start_angle)/4;
  int32_t CP_BASE_NUM = (4*sin_lookup(CP_ANG));
  int32_t CP_BASE_DENOM = (3*cos_lookup(CP_ANG));
  
  FPoint cp1_vec = FPoint( ((long long)CP_BASE_NUM*(long long)start_r*(long long) -sin_lookup(start_angle))/((long long)CP_BASE_DENOM*(long long)TRIG_MAX_RATIO),
                           ((long long)CP_BASE_NUM*(long long)start_r*(long long)  cos_lookup(start_angle))/((long long)CP_BASE_DENOM*(long long)TRIG_MAX_RATIO));
  FPoint cp2_vec = FPoint( ((long long)CP_BASE_NUM*(long long)end_r*(long long)  sin_lookup(end_angle))/((long long)CP_BASE_DENOM*(long long)TRIG_MAX_RATIO),
                           ((long long)CP_BASE_NUM*(long long)end_r*(long long) -cos_lookup(end_angle))/((long long)CP_BASE_DENOM*(long long)TRIG_MAX_RATIO));
  
  
  FPoint cp1 = fpoint_add(center, fpoint_add(start_vec, cp1_vec));
  FPoint cp2 = fpoint_add(center, fpoint_add(end_vec, cp2_vec));
  FPoint end = fpoint_add(center, end_vec);
  
//   fctx_line_to(fctx,cp1);
//   fctx_line_to(fctx,cp2);
//   fctx_line_to(fctx,end);
  fctx_curve_to(fctx, cp1, cp2, end);
}

#define SCALE_RADIUS(start_ang, end_ang, cur_ang, start_r, end_r) (start_r + ((end_r-start_r)*(cur_ang-start_ang))/(end_ang-start_ang))
static void make_spiral_dyn_width(FContext *fctx, FPoint center, fixed_t start_halfwidth, fixed_t end_halfwidth, fixed_t start_r, fixed_t end_r, int32_t start_angle, int32_t end_angle, int32_t subdivisions){
  bool positive_radians = end_angle >= start_angle;
  int32_t quad_angle = TRIG_MAX_ANGLE/subdivisions;
  int32_t flip = (positive_radians ? 1 : -1);
  int32_t quad_step = flip * quad_angle;

  fixed_t start_rph = start_r + start_halfwidth;
  fixed_t start_rmh = start_r - start_halfwidth;
  fixed_t end_rph = end_r + end_halfwidth;
  fixed_t end_rmh = end_r - end_halfwidth;
  
  fctx_move_to(fctx, get_angular_point(center, start_rph, start_angle));
  if(flip*(end_angle - start_angle) <= quad_angle){
    make_arc(fctx, center, start_rph, end_rph, start_angle, end_angle);
    fctx_line_to(fctx, get_angular_point(center, end_rmh, end_angle));
    make_arc(fctx, center, end_rmh, start_rmh, end_angle, start_angle);
  }else{
    int32_t start_delta = start_angle % quad_angle;
    if(start_delta<0)
      start_delta += quad_angle;
    
    int32_t cur_angle;
    if(positive_radians)
      cur_angle = start_angle - start_delta + quad_angle;
    else
      cur_angle = start_angle - start_delta;
    
    make_arc(fctx, center,
             start_rph,
             SCALE_RADIUS(start_angle,end_angle,cur_angle,start_rph,end_rph),
             start_angle, cur_angle);
    
    int32_t stop_angle = end_angle - quad_step;
    for(; flip*cur_angle < flip*stop_angle; cur_angle += quad_step){
      make_arc(fctx, center,
               SCALE_RADIUS(start_angle,end_angle,cur_angle,start_rph,end_rph),
               SCALE_RADIUS(start_angle,end_angle,cur_angle+quad_step,start_rph,end_rph),
               cur_angle, cur_angle+quad_step);
    }
    
    make_arc(fctx, center,
             SCALE_RADIUS(start_angle,end_angle,cur_angle,start_rph,end_rph),
             end_rph,
             cur_angle, end_angle);
    
    fctx_line_to(fctx, get_angular_point(center, end_rmh, end_angle));
    
    make_arc(fctx, center,
             end_rmh,
             SCALE_RADIUS(start_angle,end_angle,cur_angle,start_rmh,end_rmh),
             end_angle, cur_angle);
    
    stop_angle = start_angle + quad_step;
    for(; flip*cur_angle > flip*stop_angle; cur_angle -= quad_step){
      make_arc(fctx, center,
               SCALE_RADIUS(start_angle,end_angle,cur_angle,start_rmh,end_rmh),
               SCALE_RADIUS(start_angle,end_angle,cur_angle-quad_step,start_rmh,end_rmh),
               cur_angle, cur_angle-quad_step);
    }
    
    make_arc(fctx, center,
             SCALE_RADIUS(start_angle,end_angle,cur_angle,start_rmh,end_rmh),
             start_rmh,
             cur_angle, start_angle);
  }
  
  fctx_close_path(fctx);
}
static void make_spiral(FContext *fctx, FPoint center, fixed_t halfwidth, fixed_t start_r, fixed_t end_r, int32_t start_angle, int32_t end_angle, int32_t subdivisions){
  make_spiral_dyn_width(fctx, center, halfwidth, halfwidth, start_r, end_r, start_angle, end_angle, subdivisions);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-overflow"
static void make_spiral_rate(FContext *fctx, FPoint center, fixed_t halfwidth, fixed_t start_r, fixed_t delta_per_revolution, int32_t start_angle, int32_t end_angle, int32_t subdivisions){
  bool positive_radians = end_angle >= start_angle;
  fixed_t end_r = start_r + ((positive_radians ? 1 : -1) * (end_angle - start_angle) * delta_per_revolution) / TRIG_MAX_ANGLE;
  make_spiral(fctx, center, halfwidth, start_r, end_r, start_angle, end_angle, subdivisions);
}
static void make_spiral_rate_dyn_width(FContext *fctx, FPoint center, fixed_t start_halfwidth, fixed_t start_r, fixed_t d_r_per_rev, fixed_t d_h_per_rev, int32_t start_angle, int32_t end_angle, int32_t subdivisions){
  bool positive_radians = end_angle >= start_angle;
  fixed_t end_r = start_r + ((positive_radians ? 1 : -1) * (end_angle - start_angle) * d_r_per_rev) / TRIG_MAX_ANGLE;
  fixed_t end_halfwidth = start_halfwidth + ((positive_radians ? 1 : -1) * (end_angle - start_angle) * d_h_per_rev) / TRIG_MAX_ANGLE;
  make_spiral_dyn_width(fctx, center, start_halfwidth, end_halfwidth, start_r, end_r, start_angle, end_angle, subdivisions);
}
#pragma GCC diagnostic pop

static void draw_spirals_for_time(struct Layer *layer, FContext *fctx, int32_t time, int32_t time_per_twelve){
  
  int32_t inner_rad, start, end;
  
  GRect bounds = layer_get_bounds(layer);
  
  fixed_t minute_outer_radius = INT_TO_FIXED(bounds.size.w/2 - 11);
  fixed_t minute_radius_shift = -(minute_outer_radius)/14;
  fixed_t minute_halfwidth = (minute_outer_radius)/50;

  fixed_t minute_halfwidth_first_boost = minute_halfwidth * 3 / 2;
  fixed_t minute_radius_first_boost = minute_halfwidth_first_boost;

  fixed_t minute_radius_shift_first = minute_radius_shift - minute_radius_first_boost;
    
  fixed_t hour_outer_radius = minute_outer_radius + minute_radius_shift_first*3/2;
  fixed_t hour_halfwidth = INT_TO_FIXED(2);
  fixed_t hour_cover_halfwidth = INT_TO_FIXED(9)/2;
  fixed_t hour_cover_angle_boost = TRIG_MAX_ANGLE/120;
  fixed_t hour_cover_inner_hw_boost = hour_cover_halfwidth / 3;
  
  FPoint center = FPointI(bounds.origin.x+bounds.size.w/2, bounds.origin.y+bounds.size.h/2);
  
  fixed_t tick_halfwidth = INT_TO_FIXED(3)/2;
  fixed_t tick_length = INT_TO_FIXED(3);
  fixed_t tick_inner_radius = minute_outer_radius+INT_TO_FIXED(5);
  
  // make inverse minutes spiral
  fctx_set_fill_color(fctx, MINUTE_COVER_COLOR);
  
  fctx_begin_fill(fctx);
  if (time * 12 < time_per_twelve) {
    // Less than one revolution
    start = -(TRIG_MAX_ANGLE/4) + (TRIG_MAX_ANGLE * time * 12) / time_per_twelve;
    end = -(TRIG_MAX_ANGLE/4);
    make_spiral_rate_dyn_width(
      fctx,
      center,
      minute_halfwidth + minute_halfwidth_first_boost,
      minute_outer_radius,
      minute_radius_shift_first,
      -minute_halfwidth_first_boost,
      start,
      end,
      4);
    inner_rad = minute_outer_radius
      + (minute_radius_shift_first * time * 12 ) / time_per_twelve;
  } else {
    // More than one revolution
    // First revolution
    start = -(TRIG_MAX_ANGLE/4) + (TRIG_MAX_ANGLE * time * 12) / time_per_twelve;
    end = start - TRIG_MAX_ANGLE;
    make_spiral_rate_dyn_width(
      fctx,
      center,
      minute_halfwidth + minute_halfwidth_first_boost,
      minute_outer_radius,
      minute_radius_shift_first,
      -minute_halfwidth_first_boost,
      start,
      end,
      4);
    // Other revolutions
    start = end;
    end = -(TRIG_MAX_ANGLE/4);
    make_spiral_rate(
      fctx,
      center,
      minute_halfwidth,
      minute_outer_radius + minute_radius_shift_first,
      minute_radius_shift,
      start,
      end,
      4);
    inner_rad = minute_outer_radius
      + minute_radius_shift_first
      - minute_radius_shift
      + (minute_radius_shift * time * 12) / time_per_twelve;
  }
  // Outer box
  fctx_move_to(fctx, FPointI(bounds.origin.x, bounds.origin.y));
  fctx_line_to(fctx, FPointI(bounds.origin.x+bounds.size.w, bounds.origin.y));
  fctx_line_to(fctx, FPointI(bounds.origin.x+bounds.size.w, bounds.origin.y+bounds.size.h));
  fctx_line_to(fctx, FPointI(bounds.origin.x, bounds.origin.y+bounds.size.h));
  fctx_close_path(fctx);
  fctx_end_fill(fctx);
  
  if(inner_rad > hour_outer_radius)
    inner_rad = hour_outer_radius;
  start = -(TRIG_MAX_ANGLE/4) + (TRIG_MAX_ANGLE * time ) / time_per_twelve;
  end = -(TRIG_MAX_ANGLE/4);
  
  // make hours cover spiral
  fctx_begin_fill(fctx);
  make_spiral_dyn_width(
    fctx,
    center,
    hour_cover_halfwidth + hour_cover_inner_hw_boost,
    hour_cover_halfwidth,
    (hour_outer_radius - hour_cover_inner_hw_boost),
    (inner_rad),
    start,
    end,
    32);
  fctx_end_fill(fctx);
  // small extra boost to give contrast
  fctx_begin_fill(fctx);
  make_spiral_dyn_width(
    fctx,
    center,
    hour_cover_halfwidth + hour_cover_inner_hw_boost,
    hour_cover_halfwidth + hour_cover_inner_hw_boost,
    (hour_outer_radius - hour_cover_inner_hw_boost),
    (hour_outer_radius - hour_cover_inner_hw_boost),
    start+hour_cover_angle_boost,
    start,
    32);
  fctx_end_fill(fctx);
  
  fctx_set_fill_color(fctx, HOUR_SPIRAL_COLOR);
  
  // make hours spiral
  fctx_begin_fill(fctx);
  make_spiral(fctx, center, hour_halfwidth, (hour_outer_radius), (inner_rad), start, end, 32);
  fctx_end_fill(fctx);
  
  // make ticks
  fctx_set_fill_color(fctx, TICK_COLOR);
  fctx_begin_fill(fctx);
  fctx_set_offset(fctx, center);
  fixed_t clen, chw;
  for(int i=0; i<12; i++){
    if (i == 9) {
      clen = tick_length * 2;
      chw = tick_halfwidth * 2;
    } else {
      clen = tick_length;
      chw = tick_halfwidth;
    }
    fctx_set_rotation(fctx, TRIG_MAX_ANGLE * i / 12);
    fctx_move_to(fctx, FPoint(tick_inner_radius, -chw));
    fctx_line_to(fctx, FPoint(tick_inner_radius+clen, -chw));
    fctx_line_to(fctx, FPoint(tick_inner_radius+clen, +chw));
    fctx_line_to(fctx, FPoint(tick_inner_radius, +chw));
    fctx_close_path(fctx);
  }
  fctx_end_fill(fctx);
  fctx_set_offset(fctx, FPointZero);
  fctx_set_rotation(fctx, 0);
}

static void spiral_update_proc(struct Layer *layer, GContext *ctx){
  FContext _fctx, *fctx = &_fctx;
  fctx_init_context(fctx, ctx);
  
  draw_spirals_for_time(layer, fctx, minutes_ct, 12*60);
  
  fctx_deinit_context(fctx);
}

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create background layer
  #if defined(PBL_COLOR) 
    s_bgd_layer = bitmap_layer_create(bounds);
  #endif
  
  // Create a drawing layer
  s_draw_layer = layer_create(bounds);
  layer_set_update_proc(s_draw_layer, spiral_update_proc);
  
  update_graphics();

  // Add it as a child layer to the Window's root layer
  #if defined(PBL_COLOR) 
    layer_add_child(window_layer, bitmap_layer_get_layer(s_bgd_layer));
  #endif
  layer_add_child(window_layer, s_draw_layer);

  // Create our text layers
  int32_t fontsize = 18;
  int32_t buffer = 10;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  
  s_date_month_layer = text_layer_create(
      GRect(buffer, bounds.size.h - buffer - fontsize, bounds.size.w - 2 * buffer, fontsize));

  text_layer_set_background_color(s_date_month_layer, GColorClear);
  text_layer_set_text(s_date_month_layer, "--");
  text_layer_set_text_color(s_date_month_layer, GColorWhite);
  text_layer_set_font(s_date_month_layer, font);
  text_layer_set_text_alignment(s_date_month_layer, GTextAlignmentLeft);

  layer_add_child(window_layer, text_layer_get_layer(s_date_month_layer));
  
  s_date_day_layer = text_layer_create(
      GRect(buffer, bounds.size.h - buffer - fontsize, bounds.size.w - 2 * buffer, fontsize));

  text_layer_set_background_color(s_date_day_layer, GColorClear);
  text_layer_set_text(s_date_day_layer, "--");
  text_layer_set_text_color(s_date_day_layer, GColorWhite);
  text_layer_set_font(s_date_day_layer, font);
  text_layer_set_text_alignment(s_date_day_layer, GTextAlignmentRight);

  layer_add_child(window_layer, text_layer_get_layer(s_date_day_layer));

  // Set the text values
  update_text();
}

static void main_window_unload(Window *window) {
  layer_destroy(s_draw_layer);
  text_layer_destroy(s_date_month_layer);
  text_layer_destroy(s_date_day_layer);
  #if defined(PBL_COLOR) 
  gbitmap_destroy(s_bitmap_primary);
  gbitmap_destroy(s_bitmap_secondary);
  bitmap_layer_destroy(s_bgd_layer);
  #endif
}


static void init() {
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  
  // Make sure the time is displayed from the start
  update_time();

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}