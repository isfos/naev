/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @mainpage NAEV
 *
 * Doxygen documentation for the NAEV project.
 */
/**
 * @file naev.c
 *
 * @brief Controls the overall game flow: data loading/unloading and game loop.
 */

/*
 * includes
 */
/* localised global */
#include "SDL.h"
#include "SDL_image.h"

#include "naev.h"
#include "log.h" /* for DEBUGGING */


/* global */
#include <string.h> /* strdup */

#if defined(HAVE_FENV_H) && defined(DEBUGGING)
#include <fenv.h>
#endif /* defined(HAVE_FENV_H) && defined(DEBUGGING) */

#if HAS_LINUX && defined(DEBUGGING)
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>
#include <bfd.h>
#endif /* HAS_LINUX && defined(DEBUGGING) */

/* local */
#include "conf.h"
#include "physics.h"
#include "opengl.h"
#include "font.h"
#include "ship.h"
#include "pilot.h"
#include "fleet.h"
#include "player.h"
#include "input.h"
#include "joystick.h"
#include "space.h"
#include "rng.h"
#include "ai.h"
#include "outfit.h"
#include "weapon.h"
#include "faction.h"
#include "nxml.h"
#include "toolkit.h"
#include "pause.h"
#include "sound.h"
#include "music.h"
#include "spfx.h"
#include "economy.h"
#include "menu.h"
#include "mission.h"
#include "nlua_misn.h"
#include "nfile.h"
#include "nebula.h"
#include "unidiff.h"
#include "ndata.h"
#include "gui.h"
#include "news.h"
#include "nlua_var.h"
#include "map.h"
#include "event.h"
#include "cond.h"
#include "land.h"


#define CONF_FILE       "conf.lua" /**< Configuration file by default. */
#define VERSION_FILE    "VERSION" /**< Version file by default. */
#define FONT_SIZE       12 /**< Normal font size. */
#define FONT_SIZE_SMALL 10 /**< Small font size. */

#define NAEV_INIT_DELAY 3000 /**< Minimum amount of time_ms to wait with loading screen */


static int quit = 0; /**< For primary loop */
static unsigned int time_ms = 0; /**< used to calculate FPS and movement. */
static char short_version[64]; /**< Contains version. */
static char human_version[256]; /**< Human readable version. */
static glTexture *loading; /**< Loading screen. */
static char *binary_path = NULL; /**< argv[0] */
static SDL_Surface *naev_icon = NULL; /**< Icon. */
static int fps_skipped = 0; /**< Skipped last frame? */


/*
 * FPS stuff.
 */
static double fps_dt  = 1.; /**< Display fps accumulator. */
static double game_dt = 0.; /**< Current game deltatick (uses dt_mod). */
static double real_dt = 0.; /**< Real deltatick. */

#if HAS_LINUX && defined(DEBUGGING)
static bfd *abfd      = NULL;
static asymbol **syms = NULL;
#endif /* HAS_LINUX && defined(DEBUGGING) */

/*
 * prototypes
 */
/* Loading. */
static void print_SDLversion (void);
static void loadscreen_load (void);
static void loadscreen_unload (void);
static void load_all (void);
static void unload_all (void);
static void display_fps( const double dt );
static void window_caption (void);
static void debug_sigInit (void);
/* update */
static void fps_control (void);
static void update_all (void);
static void update_routine( double dt );
static void render_all (void);
/* Misc. */
void loadscreen_render( double done, const char *msg ); /* nebula.c */
void main_loop (void); /* dialogue.c */



/**
 * @brief The entry point of NAEV.
 *
 *    @param[in] argc Number of arguments.
 *    @param[in] argv Array of argc arguments.
 *    @return EXIT_SUCCESS on success.
 */
int main( int argc, char** argv )
{
   char buf[PATH_MAX];

   /* Save the binary path. */
   binary_path = strdup(argv[0]);
   
   /* Print the version */
   LOG( " "APPNAME" v%s", naev_version(0) );
#ifdef GIT_COMMIT
   DEBUG( " git HEAD at " GIT_COMMIT );
#endif /* GIT_COMMIT */

   /* Initializes SDL for possible warnings. */
   SDL_Init(0);

   /* Set up debug signal handlers. */
   debug_sigInit();

   /* Create the home directory if needed. */
   if (nfile_dirMakeExist("%s", nfile_basePath()))
      WARN("Unable to create naev directory '%s'", nfile_basePath());

   /* Must be initialized before input_init is called. */
   if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
      WARN("Unable to initialize SDL Video: %s", SDL_GetError());
      return -1;
   }

   /* Get desktop dimensions. */
#if SDL_VERSION_ATLEAST(1,2,10)
   const SDL_VideoInfo *vidinfo = SDL_GetVideoInfo();
   gl_screen.desktop_w = vidinfo->current_w;
   gl_screen.desktop_h = vidinfo->current_h;
#else /* #elif SDL_VERSION_ATLEAST(1,2,10) */
   gl_screen.desktop_w = 0;
   gl_screen.desktop_h = 0;
#endif /* #elif SDL_VERSION_ATLEAST(1,2,10) */

   /* We'll be parsing XML. */
   LIBXML_TEST_VERSION
   xmlInitParser();

   /* Input must be initialized for config to work. */
   input_init(); 

   /* Set the configuration. */
   snprintf(buf, PATH_MAX, "%s"CONF_FILE, nfile_basePath());
   conf_setDefaults(); /* set the default config values */
   conf_loadConfig(buf); /* Lua to parse the configuration file */
   conf_parseCLI( argc, argv ); /* parse CLI arguments */

   /* Enable FPU exceptions. */
#if defined(HAVE_FEENABLEEXCEPT) && defined(DEBUGGING)
   if (conf.fpu_except)
      feenableexcept( FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW );
#endif /* DEBUGGING */

   /* Open data. */
   if (ndata_open() != 0)
      ERR("Failed to open ndata.");

   /* Load the data basics. */
   LOG(" %s", ndata_name());
   DEBUG();

   /* Display the SDL Version. */
   print_SDLversion();
   DEBUG();

   /* random numbers */
   rng_init();


   /*
    * OpenGL
    */
   if (gl_init()) { /* initializes video output */
      ERR("Initializing video output failed, exiting...");
      SDL_Quit();
      exit(EXIT_FAILURE);
   }
   window_caption();
   gl_fontInit( NULL, NULL, FONT_SIZE ); /* initializes default font to size */
   gl_fontInit( &gl_smallFont, NULL, FONT_SIZE_SMALL ); /* small font */

   /* Display the load screen. */
   loadscreen_load();
   loadscreen_render( 0., "Initializing subsystems..." );
   time_ms = SDL_GetTicks();


   /*
    * Input
    */
   if ((conf.joystick_ind >= 0) || (conf.joystick_nam != NULL)) {
      if (joystick_init()) WARN("Error initializing joystick input");
      if (conf.joystick_nam != NULL) { /* use the joystick name to find a joystick */
         if (joystick_use(joystick_get(conf.joystick_nam))) {
            WARN("Failure to open any joystick, falling back to default keybinds");
            input_setDefault();
         }
         free(conf.joystick_nam);
      }
      else if (conf.joystick_ind >= 0) /* use a joystick id instead */
         if (joystick_use(conf.joystick_ind)) {
            WARN("Failure to open any joystick, falling back to default keybinds");
            input_setDefault();
         }
   }


   /*
    * OpenAL - Sound
    */
   if (conf.nosound) {
      LOG("Sound is disabled!");
      sound_disabled = 1;
      music_disabled = 1;
   }
   if (sound_init()) WARN("Problem setting up sound!");
   music_choose("load");


   /* Misc graphics init */
   if (nebu_init() != 0) { /* Initializes the nebula */
      /* An error has happened */
      ERR("Unable to initialize the Nebula subsystem!");
      /* Weirdness will occur... */
   }
   gui_init(); /* initializes the GUI graphics */
   toolkit_init(); /* initializes the toolkit */
   map_init(); /* initializes the map. */
   cond_init(); /* Initialize conditional subsystem. */

   /* Data loading */
   load_all();

   /* Unload load screen. */
   loadscreen_unload();

   /* Start menu. */
   menu_main();

   /* Force a minimum delay with loading screen */
   if ((SDL_GetTicks() - time_ms) < NAEV_INIT_DELAY)
      SDL_Delay( NAEV_INIT_DELAY - (SDL_GetTicks() - time_ms) );
   time_ms = SDL_GetTicks(); /* initializes the time_ms */
   /* 
    * main loop
    */
   SDL_Event event;
   /* flushes the event loop since I noticed that when the joystick is loaded it
    * creates button events that results in the player starting out acceling */
   while (SDL_PollEvent(&event));
   /* primary loop */
   while (!quit) {
      while (SDL_PollEvent(&event)) { /* event loop */
         if (event.type == SDL_QUIT)
            quit = 1; /* quit is handled here */

         input_handle(&event); /* handles all the events and player keybinds */
      }

      main_loop();
   }


   /* Save configuration. */
   conf_saveConfig(buf);

   /* cleanup some stuff */
   player_cleanup(); /* cleans up the player stuff */
   gui_free(); /* cleans up the player's GUI */
   weapon_exit(); /* destroys all active weapons */
   pilots_free(); /* frees the pilots, they were locked up :( */
   cond_exit(); /* destroy conditional subsystem. */
   land_exit(); /* Destroys landing vbo and friends. */

   /* data unloading */
   unload_all();

   /* cleanup opengl fonts */
   gl_freeFont(NULL);
   gl_freeFont(&gl_smallFont);

   /* Close data. */
   ndata_close();

   /* Destroy conf. */
   conf_cleanup(); /* Frees some memory the configuration allocated. */

   /* exit subsystems */
   map_exit(); /* destroys the map. */
   toolkit_exit(); /* kills the toolkit */
   ai_exit(); /* stops the Lua AI magic */
   joystick_exit(); /* releases joystick */
   input_exit(); /* cleans up keybindings */
   nebu_exit(); /* destroys the nebula */
   gl_exit(); /* kills video output */
   sound_exit(); /* kills the sound */
   news_exit(); /* destroys the news. */

   /* Free the icon. */
   if (naev_icon)
      free(naev_icon);

   SDL_Quit(); /* quits SDL */

   /* Last free. */
   free(binary_path);

   /* all is well */
   exit(EXIT_SUCCESS);
}


/**
 * @brief Loads a loading screen.
 */
void loadscreen_load (void)
{
   unsigned int i;
   char file_path[PATH_MAX];
   char **loadscreens;
   uint32_t nload;

   /* Count the loading screens */
   loadscreens = ndata_list( "gfx/loading/", &nload );

   /* Must have loading screens */
   if (nload==0) {
      WARN("No loading screens found!");
      loading = NULL;
      return;
   }

   /* Set the zoom. */
   gl_cameraZoom( conf.zoom_far );

   /* Load the texture */
   snprintf( file_path, PATH_MAX, "gfx/loading/%s", loadscreens[ RNG_SANE(0,nload-1) ] );
   loading = gl_newImage( file_path, 0 );

   /* Create the stars. */
   space_initStars( 1000 );

   /* Clean up. */
   for (i=0; i<nload; i++)
      free(loadscreens[i]);
   free(loadscreens);
}


/**
 * @brief Renders the load screen with message.
 *
 *    @param done Amount done (1. == completed).
 *    @param msg Loading screen message.
 */
void loadscreen_render( double done, const char *msg )
{
   glColour col;
   double bx,by, bw,bh;
   double x,y, w,h, rh;
   SDL_Event event;

   /* Clear background. */
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   /* Draw stars. */
   space_renderStars( 0. );

   /*
    * Dimensions.
    */
   /* Image. */
   bw = 512.;
   bh = 512.;
   bx = (SCREEN_W-bw)/2.;
   by = (SCREEN_H-bh)/2.;
   /* Loading bar. */
   w  = gl_screen.w * 0.4;
   h  = gl_screen.h * 0.02;
   rh = h + gl_defFont.h + 4.;
   x  = -w/2.;
   if (SCREEN_H < 768)
      y  = -h/2.;
   else
      y  = -bw/2 - rh - 5.;

   /* Draw loading screen image. */
   if (loading != NULL)
      gl_blitScale( loading, bx, by, bw, bh, NULL );

   /* Draw progress bar. */
   /* BG. */
   col.r = cBlack.r;
   col.g = cBlack.g;
   col.b = cBlack.b;
   col.a = 0.7;
   gl_renderRect( x-2., y-2., w+4., rh+4., &col );
   /* FG. */
   col.r = cDConsole.r;
   col.g = cDConsole.g;
   col.b = cDConsole.b;
   col.a = 0.2;
   gl_renderRect( x+done*w, y, (1.-done)*w, h, &col );
   col.r = cConsole.r;
   col.g = cConsole.g;
   col.b = cConsole.b;
   col.a = 0.7;
   gl_renderRect( x, y, done*w, h, &col );

   /* Draw text. */
   gl_printRaw( &gl_defFont, x + gl_screen.w/2., y + gl_screen.h/2 + 2. + h,
         &cConsole, msg );

   /* Flip buffers. */
   SDL_GL_SwapBuffers();

   /* Get rid of events again. */
   while (SDL_PollEvent(&event));
}


/**
 * @brief Frees the loading screen.
 */
static void loadscreen_unload (void)
{
   /* Free the textures */
   if (loading != NULL)
      gl_freeTexture(loading);
   loading = NULL;
}


/**
 * @brief Loads all the data, makes main() simpler.
 */
#define LOADING_STAGES     10. /**< Amount of loading stages. */
void load_all (void)
{
   /* order is very important as they're interdependent */
   loadscreen_render( 1./LOADING_STAGES, "Loading Commodities..." );
   commodity_load(); /* dep for space */
   loadscreen_render( 2./LOADING_STAGES, "Loading Factions..." );
   factions_load(); /* dep for fleet, space, missions, AI */
   loadscreen_render( 2./LOADING_STAGES, "Loading AI..." );
   ai_load(); /* dep for fleets */
   loadscreen_render( 3./LOADING_STAGES, "Loading Missions..." );
   missions_load(); /* no dep */
   loadscreen_render( 4./LOADING_STAGES, "Loading Events..." );
   events_load(); /* no dep */
   loadscreen_render( 5./LOADING_STAGES, "Loading Special Effects..." );
   spfx_load(); /* no dep */
   loadscreen_render( 6./LOADING_STAGES, "Loading Outfits..." );
   outfit_load(); /* dep for ships */
   loadscreen_render( 7./LOADING_STAGES, "Loading Ships..." );
   ships_load(); /* dep for fleet */
   loadscreen_render( 8./LOADING_STAGES, "Loading Fleets..." );
   fleet_load(); /* dep for space */
   loadscreen_render( 9./LOADING_STAGES, "Loading the Universe..." );
   space_load();
   loadscreen_render( 1., "Loading Completed!" );
   xmlCleanupParser(); /* Only needed to be run after all the loading is done. */
}
/**
 * @brief Unloads all data, simplifies main().
 */
void unload_all (void)
{
   /* data unloading - inverse load_all is a good order */
   economy_destroy(); /* must be called before space_exit */
   space_exit(); /* cleans up the universe itself */
   fleet_free();
   ships_free();
   outfit_free();
   spfx_free(); /* gets rid of the special effect */
   missions_free();
   factions_free();
   commodity_free();
   var_cleanup(); /* cleans up mission variables */
}

/**
 * @brief Split main loop from main() for secondary loop hack in toolkit.c.
 */
void main_loop (void)
{
   int tk;

   /* Check to see if toolkit is open. */
   tk = toolkit_isOpen();

   /* Clear buffer. */
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   fps_control(); /* everyone loves fps control */

   input_update(); /* handle key repeats. */

   sound_update( real_dt ); /* Update sounds. */
   if (tk) toolkit_update(); /* to simulate key repetition */
   if (!menu_isOpen(MENU_MAIN)) {
      if (!paused)
         update_all(); /* update game */
      render_all();
   }
   /* Toolkit is rendered on top. */
   if (tk) toolkit_render();

   gl_checkErr(); /* check error every loop */

   /* Draw buffer. */
   SDL_GL_SwapBuffers();
}


/**
 * @brief Controls the FPS.
 */
static void fps_control (void)
{
   unsigned int t;
   double delay;
   double fps_max;

   /* dt in s */
   t = SDL_GetTicks();
   real_dt  = (double)(t - time_ms); /* Get the elapsed ms. */
   real_dt /= 1000.; /* Convert to seconds. */
   game_dt  = real_dt * dt_mod; /* Apply the modifier. */
   time_ms = t;

   /* if fps is limited */                       
   if (!conf.vsync && conf.fps_max != 0) {
      fps_max = 1./(double)conf.fps_max;
      if (real_dt < fps_max) {
         delay = fps_max - real_dt;
         SDL_Delay( (unsigned int)(delay * 1000) );
         fps_dt += delay; /* makes sure it displays the proper fps */
      }
   }
}


/**
 * @brief Updates the game itself (player flying around and friends).
 *
 *    @brief Mainly uses game dt.
 */
static void update_all (void)
{
   double tempdt;
   static const double fps_min = 1./50.; /**< Minimum fps to run at. */

   if ((real_dt > 0.25) && (fps_skipped==0)) { /* slow timers down and rerun calculations */
      pause_delay((unsigned int)game_dt*1000);
      fps_skipped = 1;
      return;
   }
   else if (game_dt > fps_min) { /* we'll force a minimum of 50 FPS */

      /* First iteration. */
      tempdt = game_dt - fps_min;
      pause_delay( (unsigned int)(tempdt*1000));
      update_routine(fps_min);

      /* run as many cycles of dt=fps_min as needed */
      while (tempdt > fps_min) {
         pause_delay((unsigned int)(-fps_min*1000)); /* increment counters */
         update_routine(fps_min);
         tempdt -= fps_min;
      }

      update_routine(tempdt); /* leftovers */
      /* Note we don't touch game_dt so that fps_display works well */
   }
   else /* Standard, just update with the last dt */
      update_routine(game_dt);

   fps_skipped = 0;
}


/**
 * @brief Actually runs the updates
 *
 *    @param[in] dt Current delta tick.
 */
static void update_routine( double dt )
{
   space_update(dt);
   weapons_update(dt);
   spfx_update(dt);
   pilots_update(dt);
   missions_update(dt);
   events_update(dt);
}


/**
 * @brief Renders the game itself (player flying around and friends).
 *
 * Blitting order (layers):
 *   - BG
 *     - stars and planets
 *     - background player stuff (planet targetting)
 *     - background particles
 *     - back layer weapons
 *   - N
 *     - NPC ships
 *     - front layer weapons
 *     - normal layer particles (above ships)
 *   - FG
 *     - player
 *     - foreground particles
 *     - text and GUI
 */
static void render_all (void)
{
   double dt;

   dt = (paused) ? 0. : game_dt;

   /* setup */
   spfx_begin(dt);
   /* BG */
   space_render(dt);
   planets_render();
   weapons_render(WEAPON_LAYER_BG, dt);
   /* N */
   pilots_render(dt);
   weapons_render(WEAPON_LAYER_FG, dt);
   spfx_render(SPFX_LAYER_BACK);
   /* FG */
   player_render(dt);
   spfx_render(SPFX_LAYER_FRONT);
   space_renderOverlay(dt);
   gui_renderReticles(dt);
   pilots_renderOverlay(dt);
   spfx_end();
   gui_render(dt);
   display_fps( real_dt ); /* Exception. */
}


static double fps     = 0.; /**< FPS to finally display. */
static double fps_cur = 0.; /**< FPS accumulator to trigger change. */
/**
 * @brief Displays FPS on the screen.
 *
 *    @param[in] dt Current delta tick.
 */
static void display_fps( const double dt )
{
   double x,y;

   fps_dt  += dt;
   fps_cur += 1.;
   if (fps_dt > 1.) { /* recalculate every second */
      fps = fps_cur / fps_dt;
      fps_dt = fps_cur = 0.;
   }

   x = 15.;
   y = (double)(gl_screen.h-15-gl_defFont.h);
   if (conf.fps_show) {
      gl_print( NULL, x, y, NULL, "%3.2f", fps );
      y -= gl_defFont.h + 5.;
   }
   if (dt_mod != 1.)
      gl_print( NULL, x, y, NULL, "%3.1fx", dt_mod);
}


/**
 * @brief Sets the window caption.
 */
static void window_caption (void)
{
   char buf[PATH_MAX];
   SDL_RWops *rw;

   /* Set caption. */
   snprintf(buf, PATH_MAX ,APPNAME" - %s", ndata_name());
   SDL_WM_SetCaption(buf, APPNAME);

   /* Set icon. */
   rw = ndata_rwops( "gfx/icon.png" );
   if (rw == NULL) {
      WARN("Icon (gfx/icon.png) not found!");
      return;
   }
   naev_icon = IMG_Load_RW( rw, 1 );
   if (naev_icon == NULL) {
      WARN("Unable to load gfx/icon.png!");
      return;
   }
   SDL_WM_SetIcon( naev_icon, NULL );
}


/**
 * @brief Returns the version in a human readable string.
 *
 *    @param long_version Returns the long version if it's long.
 *    @return The human readable version string.
 */
char *naev_version( int long_version )
{
   /* Set short version if needed. */
   if (short_version[0] == '\0')
      snprintf( short_version, sizeof(short_version),
#if VREV < 0
            "%d.%d.0-beta%d",
            VMAJOR, VMINOR, ABS(VREV)
#else /* VREV < 0 */
            "%d.%d.%d",
            VMAJOR, VMINOR, VREV
#endif /* VREV < 0 */
            );

   /* Set up the long version. */
   if (long_version) {
      if (human_version[0] == '\0')
         snprintf( human_version, sizeof(human_version),
               " "APPNAME" v%s%s - %s", short_version,
#ifdef DEBUGGING
               " debug",
#else /* DEBUGGING */
               "",
#endif /* DEBUGGING */
               ndata_name() );
      return human_version;
   }

   return short_version;
}


/**
 * @brief Returns the naev binary path.
 */
char *naev_binary (void)
{
   return binary_path;
}


/**
 * @brief Prints the SDL version to console.
 */
static void print_SDLversion (void)
{
   const SDL_version *linked;
   SDL_version compiled;
   SDL_VERSION(&compiled);
   linked = SDL_Linked_Version();
   DEBUG("SDL: %d.%d.%d [compiled: %d.%d.%d]",
         linked->major, linked->minor, linked->patch,
         compiled.major, compiled.minor, compiled.patch);

   /* Check if major/minor version differ. */
   if ((linked->major*100 + linked->minor) > compiled.major*100 + compiled.minor)
      WARN("SDL is newer than compiled version");
   if ((linked->major*100 + linked->minor) < compiled.major*100 + compiled.minor)
      WARN("SDL is older than compiled version.");
}


#if HAS_LINUX && defined(DEBUGGING)
/**
 * @brief Gets the string related to the signal code.
 *
 *    @param sig Signal to which code belongs.
 *    @param sig_code Signal code to get string of.
 *    @return String of signal code.
 */
static const char* debug_sigCodeToStr( int sig, int sig_code )
{
   if (sig == SIGFPE)
      switch (sig_code) {
         case SI_USER: return "SIGFPE (raised by program)";
         case FPE_INTDIV: return "SIGFPE (integer divide by zero)";
         case FPE_INTOVF: return "SIGFPE (integer overflow)";
         case FPE_FLTDIV: return "SIGFPE (floating-point divide by zero)";
         case FPE_FLTOVF: return "SIGFPE (floating-point overflow)";
         case FPE_FLTUND: return "SIGFPE (floating-point underflow)";
         case FPE_FLTRES: return "SIGFPE (floating-point inexact result)";
         case FPE_FLTINV: return "SIGFPE (floating-point invalid operation)";
         case FPE_FLTSUB: return "SIGFPE (subscript out of range)";
         default: return "SIGFPE";
      }
   else if (sig == SIGSEGV)
      switch (sig_code) {
         case SI_USER: return "SIGSEGV (raised by program)";
         case SEGV_MAPERR: return "SIGSEGV (address not mapped to object)";
         case SEGV_ACCERR: return "SIGSEGV (invalid permissions for mapped object)";
         default: return "SIGSEGV";
      }
   else if (sig == SIGABRT)
      switch (sig_code) {
         case SI_USER: return "SIGABRT (raised by program)";
         default: return "SIGABRT";
      }

   /* No suitable code found. */
   return strsignal(sig);
}

/**
 * @brief Translates and displays the address as something humans can enjoy.
 */
static void debug_translateAddress( const char *symbol, bfd_vma address )
{
   const char *file, *func;
   unsigned int line;
   asection *section;

   for (section = abfd->sections; section != NULL; section = section->next) {
      if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
         continue;

      bfd_vma vma = bfd_get_section_vma(abfd, section);
      bfd_size_type size = bfd_get_section_size(section);
      if (address < vma || address >= vma + size)
         continue;

      if (!bfd_find_nearest_line(abfd, section, syms, address - vma,
            &file, &func, &line))
         continue;

      do {
         if (func == NULL || func[0] == '\0')
            func = "??";
         if (file == NULL || file[0] == '\0')
            file = "??";
         DEBUG("%s %s(...):%u %s", symbol, func, line, file);
      } while (bfd_find_inliner_info(abfd, &file, &func, &line));

      return;
   }

   DEBUG("%s %s(...):%u %s", symbol, "??", 0, "??");
}


/**
 * @brief Backtrace signal handler for Linux.
 *
 *    @param sig Signal.
 *    @param info Signal information.
 *    @param unused Unused.
 */
static void debug_sigHandler( int sig, siginfo_t *info, void *unused )
{
   (void)sig;
   (void)unused;
   int i, num;
   void *buf[64];
   char **symbols;

   num      = backtrace(buf, 64);
   symbols  = backtrace_symbols(buf, num);

   DEBUG("NAEV recieved %s!",
         debug_sigCodeToStr(info->si_signo, info->si_code) );
   for (i=0; i<num; i++) {
      if (abfd != NULL)
         debug_translateAddress(symbols[i], (bfd_vma) (bfd_hostptr_t) buf[i]);
      else
         DEBUG("   %s", symbols[i]);
   }
   DEBUG("Report this to project maintainer with the backtrace.");

   /* Always exit. */
   exit(1);
}
#endif /* HAS_LINUX && defined(DEBUGGING) */


/**
 * @brief Sets up the SignalHandler for Linux.
 */
static void debug_sigInit (void)
{
#if HAS_LINUX && defined(DEBUGGING)
   char **matching;
   struct sigaction sa, so;

   bfd_init();

   /* Read the executable */
   abfd = bfd_openr("/proc/self/exe", NULL);
   if (abfd != NULL) {
      bfd_check_format_matches(abfd, bfd_object, &matching);

      /* Read symbols */
      if (bfd_get_file_flags(abfd) & HAS_SYMS) {
         long symcount;
         unsigned int size;

         /* static */
         symcount = bfd_read_minisymbols (abfd, FALSE, (void **)&syms, &size);
         if (symcount == 0) /* dynamic */
            symcount = bfd_read_minisymbols (abfd, TRUE, (void **)&syms, &size);
         assert(symcount >= 0);
      }
   }

   /* Set up handler. */
   sa.sa_handler   = NULL;
   sa.sa_sigaction = debug_sigHandler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags     = SA_SIGINFO;

   /* Attach signals. */
   sigaction(SIGSEGV, &sa, &so);
   if (so.sa_handler == SIG_IGN)
      DEBUG("Unable to set up SIGSEGV signal handler.");
   sigaction(SIGFPE, &sa, &so);
   if (so.sa_handler == SIG_IGN)
      DEBUG("Unable to set up SIGFPE signal handler.");
   sigaction(SIGABRT, &sa, &so);
   if (so.sa_handler == SIG_IGN)
      DEBUG("Unable to set up SIGABRT signal handler.");
#endif /* HAS_LINUX && defined(DEBUGGING) */
}
