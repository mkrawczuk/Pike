//!

GTK1.Style apply_default_background( GDK1.Window window, int set_bgp, int state_type, GDK1.Rectangle area, int x, int y, int width, int height );
//! Set the background of the specified window (or the subarea
//! indicated by the rectangle) to the default background for the state
//! specified by state_type.
//! 
//! If set_bgp is true, the background of the widget will be set,
//! otherwise it will only be drawn into the window.
//!
//!

GTK1.Style attach( GDK1.Window to );
//!   Attach a style to a window; this process allocates the colors and
//!   creates the GC's for the style - it specializes it to a
//!   particular visual and colormap. The process may involve the
//!   creation of a new style if the style has already been attached to
//!   a window with a different style and colormap.
//!
//!

GTK1.Style copy( );
//!  Copy this style, and return the new style object
//!
//!

GTK1.Style destroy( );
//!

GTK1.Style detach( );
//!  Undo a previous attach
//!
//!

array get_base( );
//!

array get_base_gc( );
//!

array get_bg( );
//!

array get_bg_gc( );
//!

array get_bg_pixmap( );
//!

GDK1.Color get_black( );
//!

GDK1.GC get_black_gc( );
//!

array get_dark( );
//!

array get_dark_gc( );
//!

array get_fg( );
//!

array get_fg_gc( );
//!

GDK1.Font get_font( );
//!

array get_light( );
//!

array get_light_gc( );
//!

array get_mid( );
//!

array get_mid_gc( );
//!

array get_text( );
//!

array get_text_gc( );
//!

GDK1.Color get_white( );
//!

GDK1.GC get_white_gc( );
//!
