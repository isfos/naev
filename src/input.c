/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file input.c
 *
 * @brief Handles all the keybindings and input.
 */


#include "input.h"

#include "naev.h"

#include "log.h"
#include "player.h"
#include "pause.h"
#include "toolkit.h"
#include "menu.h"
#include "board.h"
#include "map.h"
#include "escort.h"
#include "land.h"
#include "nstd.h"
#include "gui.h"
#include "weapon.h"
#include "console.h"
#include "conf.h"


#define KEY_PRESS    ( 1.) /**< Key is pressed. */
#define KEY_RELEASE  (-1.) /**< Key is released. */


/* keybinding structure */
/**
 * @brief NAEV Keybinding.
 */
typedef struct Keybind_ {
   const char *name; /**< keybinding name, taken from keybindNames */
   KeybindType type; /**< type, defined in player.h */
   SDLKey key; /**< key/axis/button event number */
   SDLMod mod; /**< Key modifiers (where applicable). */
} Keybind;


static Keybind** input_keybinds; /**< contains the players keybindings */

/* name of each keybinding */
const char *keybindNames[] = {
   /* Movement. */
   "accel", "left", "right", "reverse", "afterburn",
   /* Targetting. */
   "target_next", "target_prev", "target_nearest",
   "target_nextHostile", "target_prevHostile", "target_hostile",
   "target_clear",
   /* Fighting. */
   "primary", "face", "board", "safety",
   /* Weapon selection. */
   "weap_all", "weap_turret", "weap_forward",
   /* Secondary weapons. */
   "secondary", "secondary_next", "secondary_prev",
   /* Escorts. */
   "e_targetNext", "e_targetPrev", "e_attack", "e_hold", "e_return", "e_clear",
   /* Space navigation. */
   "autonav", "target_planet", "land", "thyperspace", "starmap", "jump",
   /* Communication. */
   "log_up", "log_down", "hail", "autohail",
   /* Misc. */
   "mapzoomin", "mapzoomout", "screenshot", "pause", "speed", "menu", "info",
   "console", "switchtab1", "switchtab2", "switchtab3", "switchtab4",
   "switchtab5", "switchtab6", "switchtab7", "switchtab8", "switchtab9",
   "switchtab0",
   /* Must terminate in "end". */
   "end"
}; /**< Names of possible keybindings. */
/*
 * Keybinding descriptions. Should match in position the names.
 */
const char *keybindDescription[] = {
   /* Movement. */
   "Makes your ship accelerate forward.",
   "Makes your ship turn left.",
   "Makes your ship turn right.",
   "Makes your ship turn around and face the direction you're moving from. Good for braking.",
   "Makes your ship afterburn if you have an afterburner installed.",
   /* Targetting. */
   "Cycles through ship targets.",
   "Cycles backwards through ship targets.",
   "Targets the nearest non-disabled ship.",
   "Cycles through hostile ship targets.",
   "Cycles backwards through hostile ship targets.",
   "Targets the nearest hostile ship.",
   "Clears current target.",
   /* Fighting. */
   "Fires your primary weapons.",
   "Faces your target (ship target if you have one, otherwise your planet target).",
   "Attempts to board your target ship.",
   "Toggles weapon safety (hitting of friendly ships).",
   /* Weapon selection. */
   "Sets fire mode to use all weapons available (both turret and forward mounts).",
   "Sets fire mode to only use turret-class primary weapons.",
   "Sets fire mode to only use forward-class primary weapons.",
   /* Secondary weapons. */
   "Fires your secondary weapon.",
   "Cycles through secondary weapons.",
   "Cycles backwards through secondary weapons.",
   /* Escorts. */
   "Cycles through your escorts.",
   "Cycles backwards through your escorts.",
   "Tells your escorts to attack your target.",
   "Tells your escorts to hold their positions.",
   "Tells your escorts to return to your ship hangars.",
   "Clears your escorts of commands.",
   /* Space navigation. */
   "Initializes the autonavigation system.",
   "Cycles through planet targets",
   "Attempts to land on your targetted planet or targets the nearest landable planet. Requests for landing if you don't have permission yet.",
   "Cycles through hyperspace targets.",
   "Opens the Star Map.",
   "Attempts to jump to your hyperspace target.",
   /* Communication. */
   "Scrolls the log upwards.",
   "Scrolls the log downwards.",
   "Attempts to initialize communication with your targetted ship.",
   "Automatically initialize communication with a ship that hailed you.",
   /* Misc. */
   "Zooms in on your radar.",
   "Zooms out on your radar.",
   "Takes a screenshot.",
   "Pauses the game.",
   "Toggles 2x speed modifier.",
   "Opens the small ingame menu.",
   "Opens the information menu.",
   "Opens the Lua console.",
   "Switches to tab 1.",
   "Switches to tab 2.",
   "Switches to tab 3.",
   "Switches to tab 4.",
   "Switches to tab 5.",
   "Switches to tab 6.",
   "Switches to tab 7.",
   "Switches to tab 8.",
   "Switches to tab 9.",
   "Switches to tab 10.",
   NULL /* To match sentinel. */
}; /**< Descriptions of the keybindings. Should be in the same position as the
        matching keybinding name. */


/*
 * accel hacks
 */
static unsigned int input_accelLast = 0; /**< Used to see if double tap */
static int input_afterburnerButton  = 0; /**< Used to see if afterburner button is pressed. */


/*
 * Key repeat hack.
 */
static int repeat_key                  = -1; /**< Key to repeat. */
static unsigned int repeat_keyTimer    = 0;  /**< Repeat timer. */
static unsigned int repeat_keyCounter  = 0;  /**< Counter for key repeats. */


/*
 * from player.c
 */
extern double player_left;  /**< player.c */
extern double player_right; /**< player.c */


/*
 * Key conversion table.
 */
#if SDL_VERSION_ATLEAST(1,3,0)
#  define INPUT_NUMKEYS     SDL_NUM_SCANCODES /**< Number of keys available. */
#else /* SDL_VERSION_ATLEAST(1,3,0) */
#  define INPUT_NUMKEYS     SDLK_LAST /**< Number of keys available. */
#endif /* SDL_VERSION_ATLEAST(1,3,0) */
static char *keyconv[INPUT_NUMKEYS]; /**< Key conversion table. */


/*
 * Prototypes.
 */
static void input_keyConvGen (void);
static void input_keyConvDestroy (void);
static void input_key( int keynum, double value, double kabs, int repeat );


/**
 * @brief Sets the default input keys.
 */
void input_setDefault (void)
{
   /* Movement. */
   input_setKeybind( "accel", KEYBIND_KEYBOARD, SDLK_UP, NMOD_ALL );
   input_setKeybind( "afterburn", KEYBIND_KEYBOARD, SDLK_z, NMOD_ALL );
   input_setKeybind( "left", KEYBIND_KEYBOARD, SDLK_LEFT, NMOD_ALL );
   input_setKeybind( "right", KEYBIND_KEYBOARD, SDLK_RIGHT, NMOD_ALL );
   input_setKeybind( "reverse", KEYBIND_KEYBOARD, SDLK_DOWN, NMOD_ALL );
   /* Targetting. */
   input_setKeybind( "target_next", KEYBIND_KEYBOARD, SDLK_TAB, NMOD_NONE );
   input_setKeybind( "target_prev", KEYBIND_KEYBOARD, SDLK_TAB, NMOD_CTRL );
   input_setKeybind( "target_nearest", KEYBIND_KEYBOARD, SDLK_t, NMOD_NONE );
   input_setKeybind( "target_nextHostile", KEYBIND_KEYBOARD, SDLK_r, NMOD_CTRL );
   input_setKeybind( "target_prevHostile", KEYBIND_NULL, SDLK_UNKNOWN, NMOD_NONE );
   input_setKeybind( "target_hostile", KEYBIND_KEYBOARD, SDLK_r, NMOD_NONE );
   input_setKeybind( "target_clear", KEYBIND_KEYBOARD, SDLK_BACKSPACE, NMOD_ALL );
   /* Combat. */
   input_setKeybind( "primary", KEYBIND_KEYBOARD, SDLK_SPACE, NMOD_ALL );
   input_setKeybind( "face", KEYBIND_KEYBOARD, SDLK_a, NMOD_ALL );
   input_setKeybind( "board", KEYBIND_KEYBOARD, SDLK_b, NMOD_NONE );
   input_setKeybind( "safety", KEYBIND_KEYBOARD, SDLK_s, NMOD_CTRL );
   /* Weapon selection. */
   input_setKeybind( "weap_all", KEYBIND_KEYBOARD, SDLK_1, NMOD_NONE );
   input_setKeybind( "weap_turret", KEYBIND_KEYBOARD, SDLK_2, NMOD_NONE );
   input_setKeybind( "weap_forward", KEYBIND_KEYBOARD, SDLK_3, NMOD_NONE );
   /* Secondary weapons. */
   input_setKeybind( "secondary", KEYBIND_KEYBOARD, SDLK_LSHIFT, NMOD_ALL );
   input_setKeybind( "secondary_next", KEYBIND_KEYBOARD, SDLK_w, NMOD_NONE );
   input_setKeybind( "secondary_prev", KEYBIND_KEYBOARD, SDLK_w, NMOD_CTRL );
   /* Escorts. */
   input_setKeybind( "e_targetNext", KEYBIND_KEYBOARD, SDLK_e, NMOD_NONE );
   input_setKeybind( "e_targetPrev", KEYBIND_KEYBOARD, SDLK_e, NMOD_CTRL );
   input_setKeybind( "e_attack", KEYBIND_KEYBOARD, SDLK_f, NMOD_ALL );
   input_setKeybind( "e_hold", KEYBIND_KEYBOARD, SDLK_g, NMOD_ALL );
   input_setKeybind( "e_return", KEYBIND_KEYBOARD, SDLK_c, NMOD_CTRL );
   input_setKeybind( "e_clear", KEYBIND_KEYBOARD, SDLK_c, NMOD_NONE );
   /* Space. */
   input_setKeybind( "autonav", KEYBIND_KEYBOARD, SDLK_j, NMOD_CTRL );
   input_setKeybind( "target_planet", KEYBIND_KEYBOARD, SDLK_p, NMOD_NONE );
   input_setKeybind( "land", KEYBIND_KEYBOARD, SDLK_l, NMOD_NONE );
   input_setKeybind( "thyperspace", KEYBIND_KEYBOARD, SDLK_h, NMOD_NONE );
   input_setKeybind( "starmap", KEYBIND_KEYBOARD, SDLK_m, NMOD_NONE );
   input_setKeybind( "jump", KEYBIND_KEYBOARD, SDLK_j, NMOD_NONE );
   /* Communication. */
   input_setKeybind( "log_up", KEYBIND_KEYBOARD, SDLK_PAGEUP, NMOD_ALL );
   input_setKeybind( "log_down", KEYBIND_KEYBOARD, SDLK_PAGEDOWN, NMOD_ALL );
   input_setKeybind( "hail", KEYBIND_KEYBOARD, SDLK_y, NMOD_NONE );
   input_setKeybind( "autohail", KEYBIND_KEYBOARD, SDLK_y, NMOD_CTRL );
   /* Misc. */
   input_setKeybind( "mapzoomin", KEYBIND_KEYBOARD, SDLK_KP_PLUS, NMOD_ALL );
   input_setKeybind( "mapzoomout", KEYBIND_KEYBOARD, SDLK_KP_MINUS, NMOD_ALL );
   input_setKeybind( "screenshot", KEYBIND_KEYBOARD, SDLK_KP_MULTIPLY, NMOD_ALL );
   input_setKeybind( "pause", KEYBIND_KEYBOARD, SDLK_PAUSE, NMOD_ALL );
   input_setKeybind( "speed", KEYBIND_KEYBOARD, SDLK_BACKQUOTE, NMOD_ALL );
   input_setKeybind( "menu", KEYBIND_KEYBOARD, SDLK_ESCAPE, NMOD_ALL );
   input_setKeybind( "info", KEYBIND_KEYBOARD, SDLK_i, NMOD_NONE );
   input_setKeybind( "console", KEYBIND_KEYBOARD, SDLK_F2, NMOD_ALL );
   input_setKeybind( "switchtab1", KEYBIND_KEYBOARD, SDLK_1, NMOD_ALT );
   input_setKeybind( "switchtab2", KEYBIND_KEYBOARD, SDLK_2, NMOD_ALT );
   input_setKeybind( "switchtab3", KEYBIND_KEYBOARD, SDLK_3, NMOD_ALT );
   input_setKeybind( "switchtab4", KEYBIND_KEYBOARD, SDLK_4, NMOD_ALT );
   input_setKeybind( "switchtab5", KEYBIND_KEYBOARD, SDLK_5, NMOD_ALT );
   input_setKeybind( "switchtab6", KEYBIND_KEYBOARD, SDLK_6, NMOD_ALT );
   input_setKeybind( "switchtab7", KEYBIND_KEYBOARD, SDLK_7, NMOD_ALT );
   input_setKeybind( "switchtab8", KEYBIND_KEYBOARD, SDLK_8, NMOD_ALT );
   input_setKeybind( "switchtab9", KEYBIND_KEYBOARD, SDLK_9, NMOD_ALT );
   input_setKeybind( "switchtab0", KEYBIND_KEYBOARD, SDLK_0, NMOD_ALT );
}


/**
 * @brief Initializes the input subsystem (does not set keys).
 */
void input_init (void)
{  
   Keybind *temp;
   int i;

   /* We need unicode for the input widget. */
   SDL_EnableUNICODE(1);

   /* Key repeat fscks up stuff like double tap. */
   SDL_EnableKeyRepeat( 0, 0 );

#ifdef DEBUGGING
   /* To avoid stupid segfaults like in the 0.3.6 release. */
   if (sizeof(keybindNames) != sizeof(keybindDescription)) {
      WARN("Keybind names and descriptions aren't of the same size!");
      WARN("   %u descriptions for %u names",
            (unsigned int) sizeof(keybindNames),
            (unsigned int) sizeof(keybindDescription));
   }
#endif /* DEBUGGING */

   /* Window. */
   SDL_EventState( SDL_SYSWMEVENT,      SDL_DISABLE );

   /* Keyboard. */
   SDL_EventState( SDL_KEYDOWN,         SDL_ENABLE );
   SDL_EventState( SDL_KEYUP,           SDL_ENABLE );

   /* Mice. */
   SDL_EventState( SDL_MOUSEMOTION,     SDL_ENABLE );
   SDL_EventState( SDL_MOUSEBUTTONDOWN, SDL_ENABLE );
   SDL_EventState( SDL_MOUSEBUTTONUP,   SDL_ENABLE );
   
   /* Joystick, enabled in joystick.c if needed. */
   SDL_EventState( SDL_JOYAXISMOTION,   SDL_DISABLE );
   SDL_EventState( SDL_JOYHATMOTION,    SDL_DISABLE );
   SDL_EventState( SDL_JOYBUTTONDOWN,   SDL_DISABLE );
   SDL_EventState( SDL_JOYBUTTONUP,     SDL_DISABLE );

   /* Quit. */
   SDL_EventState( SDL_QUIT,            SDL_ENABLE );

#if SDL_VERSION_ATLEAST(1,3,0)
   /* Window. */
   SDL_EventState( SDL_WINDOWEVENT,     SDL_DISABLE );

   /* Keyboard. */
   SDL_EventState( SDL_TEXTINPUT,       SDL_DISABLE );

   /* Mouse. */
   SDL_EventState( SDL_MOUSEWHEEL,      SDL_DISABLE );

   /* Proximity. */
   SDL_EventState( SDL_PROXIMITYIN,     SDL_DISABLE );
   SDL_EventState( SDL_PROXIMITYOUT,    SDL_DISABLE );
#endif /* SDL_VERSION_ATLEAST(1,3,0) */

   /* Get the number of keybindings. */
   for (i=0; strcmp(keybindNames[i],"end"); i++);
      input_keybinds = malloc(i*sizeof(Keybind*));

   /* Create sane null keybinding for each. */
   for (i=0; strcmp(keybindNames[i],"end"); i++) {
      temp = malloc(sizeof(Keybind));
      memset( temp, 0, sizeof(Keybind) );
      temp->name        = keybindNames[i];
      temp->type        = KEYBIND_NULL;
      temp->key         = SDLK_UNKNOWN;
      temp->mod         = NMOD_NONE;
      input_keybinds[i] = temp;
   }

   /* Generate Key translation table. */
   input_keyConvGen();
}


/**
 * @brief Exits the input subsystem.
 */
void input_exit (void)
{
   int i;
   for (i=0; strcmp(keybindNames[i],"end"); i++)
      free(input_keybinds[i]);
   free(input_keybinds);

   input_keyConvDestroy();
}


/**
 * @brief Creates the key conversion table.
 */
static void input_keyConvGen (void)
{
   SDLKey k;

   for (k=0; k < INPUT_NUMKEYS; k++)
      keyconv[k] = strdup( SDL_GetKeyName(k) );
}


/**
 * @brief Destroys the key conversion table.
 */
static void input_keyConvDestroy (void)
{
   int i;

   for (i=0; i < INPUT_NUMKEYS; i++)
      free( keyconv[i] );
}


/**
 * @brief Gets the key id from it's name.
 *
 *    @param name Name of the key to get id from.
 *    @return ID of the key.
 */
SDLKey input_keyConv( const char *name )
{
   SDLKey k, m;
   size_t l;
   char buf;

   l = strlen(name);
   buf = tolower(name[0]);

   /* Compare for single character. */
   if (l == 1) {
      m = MIN(256, INPUT_NUMKEYS);
      for (k=0; k < m; k++) { /* Only valid for char range. */
         /* Must not be NULL. */
         if (keyconv[k] == NULL)
            continue;

         /* Check if is also a single char. */
         if ((buf == tolower(keyconv[k][0])) && (keyconv[k][1] == '\0'))
            return k;
      }
   }
   /* Compare for strings. */
   else {
      for (k=0; k < INPUT_NUMKEYS; k++) {
         /* Must not be NULL. */
         if (keyconv[k] == NULL)
            continue;

         /* Compare strings. */
         if (strcmp(name , keyconv[k])==0)
            return k;
      }
   }

   WARN("Keyname '%s' doesn't match any key.", name);
   return SDLK_UNKNOWN;
}


/**
 * @brief Binds key of type type to action keybind.
 *
 *    @param keybind The name of the keybind defined above.
 *    @param type The type of the keybind.
 *    @param key The key to bind to.
 *    @param mod Modifiers to check for.
 */
void input_setKeybind( const char *keybind, KeybindType type, int key, SDLMod mod )
{  
   int i;
   for (i=0; strcmp(keybindNames[i],"end"); i++)
      if (strcmp(keybind, input_keybinds[i]->name)==0) {
         input_keybinds[i]->type = type;
         input_keybinds[i]->key = key;
         /* Non-keyboards get mod NMOD_ALL to always match. */
         input_keybinds[i]->mod = (type==KEYBIND_KEYBOARD) ? mod : NMOD_ALL;
         return;
      }
   WARN("Unable to set keybinding '%s', that command doesn't exist", keybind);
}


/**
 * @brief Gets the value of a keybind.
 *
 *    @brief keybind Name of the keybinding to get.
 *    @param[out] type Stores the type of the keybinding.
 *    @param[out] mod Stores the modifiers used with the keybinding.
 *    @return The key assosciated with the keybinding.
 */
SDLKey input_getKeybind( const char *keybind, KeybindType *type, SDLMod *mod )
{
   int i;
   for (i=0; strcmp(keybindNames[i],"end"); i++)
      if (strcmp(keybind, input_keybinds[i]->name)==0) {
         if (type != NULL)
            (*type) = input_keybinds[i]->type;
         if (mod != NULL)
            (*mod) = input_keybinds[i]->mod;
         return input_keybinds[i]->key;
      }
   WARN("Unable to get keybinding '%s', that command doesn't exist", keybind);
   return (SDLKey)-1;
}


/**
 * @brief Gets the human readable version of mod.
 *
 *    @brief mod Mod to get human readable version from.
 *    @return Human readable version of mod.
 */
const char* input_modToText( SDLMod mod )
{
   switch (mod) {
      case NMOD_NONE:   return "None";
      case NMOD_CTRL:   return "Ctrl";
      case NMOD_SHIFT:  return "Shift";
      case NMOD_ALT:    return "Alt";
      case NMOD_META:   return "Meta";
      case NMOD_ALL:    return "Any";
      default:          return "unknown";
   }
}


/**
 * @brief Checks to see if a key is already bound.
 *
 *    @param type Type of key.
 *    @param key Key.
 *    @param mod Key modifiers.
 *    @return Name of the key that is already bound to it.
 */
const char *input_keyAlreadyBound( KeybindType type, int key, SDLMod mod )
{
   int i;
   Keybind *k;
   for (i=0; strcmp(keybindNames[i],"end"); i++) {
      k = input_keybinds[i];

      /* Type must match. */
      if (k->type != type)
         continue;

      /* Must match key. */
      if (key != (int)k->key)
         continue;

      /* Handle per case. */
      switch (type) {
         case KEYBIND_KEYBOARD:
            if ((k->mod == NMOD_ALL) || (mod == NMOD_ALL) ||
                  (k->mod == mod))
               return keybindNames[i];
            break;

         case KEYBIND_JAXISPOS:
         case KEYBIND_JAXISNEG:
         case KEYBIND_JBUTTON:
            return keybindNames[i];

         default:
            break;
      }
   }

   /* Not found. */
   return NULL;
}


/**
 * @brief Gets the description of the keybinding.
 *
 *    @param keybind Keybinding to get the description of.
 *    @return Description of the keybinding.
 */
const char* input_getKeybindDescription( const char *keybind )
{
   int i;
   for (i=0; strcmp(keybindNames[i],"end"); i++)
      if (strcmp(keybind, input_keybinds[i]->name)==0)
         return keybindDescription[i];
   WARN("Unable to get keybinding description '%s', that command doesn't exist", keybind);
   return NULL;
}


/**
 * @brief Translates SDL modifier to NAEV modifier.
 *
 *    @param mod SDL modifier to translate.
 *    @return NAEV modifier.
 */
SDLMod input_translateMod( SDLMod mod )
{
   SDLMod mod_filtered = 0;
   if (mod & (KMOD_LSHIFT | KMOD_RSHIFT))
      mod_filtered |= NMOD_SHIFT;
   if (mod & (KMOD_LCTRL | KMOD_RCTRL))
      mod_filtered |= NMOD_CTRL;
   if (mod & (KMOD_LALT | KMOD_RALT))
      mod_filtered |= NMOD_ALT;
   if (mod & (KMOD_LMETA | KMOD_RMETA))
      mod_filtered |= NMOD_META;
   return mod_filtered;
}


/**
 * @brief Handles key repeating.
 */
void input_update (void)
{
   unsigned int t;

   /* Must not be disabled. */
   if (conf.repeat_delay == 0)
      return;

   /* Key must be repeating. */
   if (repeat_key == -1)
      return;

   /* Get time. */
   t = SDL_GetTicks();

   /* Should be repeating. */
   if (repeat_keyTimer + conf.repeat_delay + repeat_keyCounter*conf.repeat_freq > t)
      return;

   /* Key repeat. */
   repeat_keyCounter++;
   input_key( repeat_key, KEY_PRESS, 0., 1 );
}


#define KEY(s)    (strcmp(input_keybinds[keynum]->name,s)==0) /**< Shortcut for ease. */
#define INGAME()  (!toolkit_isOpen() && !paused) /**< Makes sure player is in game. */
#define NOHYP()   \
(player && !pilot_isFlag(player,PILOT_HYP_PREP) &&\
!pilot_isFlag(player,PILOT_HYP_BEGIN) &&\
!pilot_isFlag(player,PILOT_HYPERSPACE)) /**< Make sure the player isn't jumping. */
#define NODEAD()  (player && !pilot_isFlag(player,PILOT_DEAD)) /**< Player isn't dead. */
#define NOLAND()  (!landed) /**< Player isn't landed. */
/**
 * @brief Runs the input command.
 *
 *    @param keynum The index of the  keybind.
 *    @param value The value of the keypress (defined above).
 *    @param kabs The absolute value.
 */
static void input_key( int keynum, double value, double kabs, int repeat )
{
   unsigned int t;

   /* Repetition stuff. */
   if (conf.repeat_delay != 0) {
      if ((value == KEY_PRESS) && !repeat) {
         repeat_key        = keynum;
         repeat_keyTimer   = SDL_GetTicks();
         repeat_keyCounter = 0;
      }
      else if (value == KEY_RELEASE) {
         repeat_key        = -1;
         repeat_keyTimer   = 0;
         repeat_keyCounter = 0;
      }
   }

   /*
    * movement
    */
   /* accelerating */
   if (KEY("accel") && !repeat) {
      if (kabs >= 0.) {
         player_abortAutonav(NULL);
         player_accel(kabs);
      }
      else { /* prevent it from getting stuck */
         if (value==KEY_PRESS) {
            player_abortAutonav(NULL);
            player_accel(1.);
         }
            
         else if (value==KEY_RELEASE)
            player_accelOver();

         /* double tap accel = afterburn! */
         t = SDL_GetTicks();
         if ((conf.afterburn_sens != 0) &&
               (value==KEY_PRESS) && INGAME() && NOHYP() && NODEAD() &&
               (t-input_accelLast <= conf.afterburn_sens))
            player_afterburn();
         else if ((value==KEY_RELEASE) && !input_afterburnerButton)
            player_afterburnOver();

         if (value==KEY_PRESS)
            input_accelLast = t;
      }
   /* Afterburning. */
   } else if (KEY("afterburn") && INGAME() && !repeat) {
      if ((value==KEY_PRESS) && NOHYP() && NODEAD()) {
         player_afterburn();
         input_afterburnerButton = 1;
      }
      else if (value==KEY_RELEASE) {
         player_afterburnOver();
         input_afterburnerButton = 0;
      }

   /* turning left */
   } else if (KEY("left") && !repeat) {
      if (kabs >= 0.) {
         player_abortAutonav(NULL);
         player_setFlag(PLAYER_TURN_LEFT); 
         player_left = kabs;
      }
      else {
         /* set flags for facing correction */
         if (value==KEY_PRESS) { 
            player_abortAutonav(NULL);
            player_setFlag(PLAYER_TURN_LEFT); 
            player_left = 1.;
         }
         else if (value==KEY_RELEASE) {
            player_rmFlag(PLAYER_TURN_LEFT);
            player_left = 0.;
         }
      }

   /* turning right */
   } else if (KEY("right") && !repeat) {
      if (kabs >= 0.) {
         player_abortAutonav(NULL);
         player_setFlag(PLAYER_TURN_RIGHT);
         player_right = kabs;
      }
      else {
         /* set flags for facing correction */
         if (value==KEY_PRESS) {
            player_abortAutonav(NULL);
            player_setFlag(PLAYER_TURN_RIGHT);
            player_right = 1.;
         }
         else if (value==KEY_RELEASE) {
            player_rmFlag(PLAYER_TURN_RIGHT);
            player_right = 0.;
         }
      }
   
   /* turn around to face vel */
   } else if (KEY("reverse") && !repeat) {
      if (value==KEY_PRESS) {
         player_abortAutonav(NULL);
         player_setFlag(PLAYER_REVERSE);
      }
      else if ((value==KEY_RELEASE) && player_isFlag(PLAYER_REVERSE))
         player_rmFlag(PLAYER_REVERSE);


   /*
    * combat
    */
   /* shooting primary weapon */
   } else if (KEY("primary") && NODEAD() && !repeat) {
      if (value==KEY_PRESS) { 
         player_abortAutonav(NULL);
         player_setFlag(PLAYER_PRIMARY);
      }
      else if (value==KEY_RELEASE) 
         player_rmFlag(PLAYER_PRIMARY);
   /* targetting */
   } else if (INGAME() && NODEAD() && KEY("target_next")) {
      if (value==KEY_PRESS) player_targetNext(0);
   } else if (INGAME() && NODEAD() && KEY("target_prev")) {
      if (value==KEY_PRESS) player_targetPrev(0);
   } else if (INGAME() && NODEAD() && KEY("target_nearest")) {
      if (value==KEY_PRESS) player_targetNearest();
   } else if (INGAME() && NODEAD() && KEY("target_nextHostile")) {
      if (value==KEY_PRESS) player_targetNext(1);
   } else if (INGAME() && NODEAD() && KEY("target_prevHostile")) {
      if (value==KEY_PRESS) player_targetPrev(1);
   } else if (INGAME() && NODEAD() && KEY("target_hostile")) {
      if (value==KEY_PRESS) player_targetHostile();
   } else if (INGAME() && NODEAD() && KEY("target_clear")) {
      if (value==KEY_PRESS) player_targetClear();
   /* face the target */
   } else if (KEY("face") && !repeat) {
      if (value==KEY_PRESS) { 
         player_abortAutonav(NULL);
         player_setFlag(PLAYER_FACE);
      }
      else if ((value==KEY_RELEASE) && player_isFlag(PLAYER_FACE))
         player_rmFlag(PLAYER_FACE);

   /* board them ships */
   } else if (KEY("board") && INGAME() && NOHYP() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) {
         player_abortAutonav(NULL);
         player_board();
      }
   } else if (KEY("safety") && INGAME() && !repeat) {
      if (value==KEY_PRESS)
         weapon_toggleSafety();


   /* 
    * Weapon selection.
    */
   } else if (KEY("weap_all") && INGAME() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) player_setFireMode( 0 );
   } else if (KEY("weap_turret") && INGAME() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) player_setFireMode( 1 );
   } else if (KEY("weap_forward") && INGAME() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) player_setFireMode( 2 );


   /*
    * Escorts.
    */
   } else if (INGAME() && NODEAD() && KEY("e_targetNext") && !repeat) {
      if (value==KEY_PRESS) player_targetEscort(0);
   } else if (INGAME() && NODEAD() && KEY("e_targetPrev") && !repeat) {
      if (value==KEY_PRESS) player_targetEscort(1);
   } else if (INGAME() && NODEAD() && KEY("e_attack") && !repeat) {
      if (value==KEY_PRESS) escorts_attack(player);
   } else if (INGAME() && NODEAD() && KEY("e_hold") && !repeat) {
      if (value==KEY_PRESS) escorts_hold(player);
   } else if (INGAME() && NODEAD() && KEY("e_return") && !repeat) {
      if (value==KEY_PRESS) escorts_return(player);
   } else if (INGAME() && NODEAD() && KEY("e_clear") && !repeat) {
      if (value==KEY_PRESS) escorts_clear(player);


   /*
    * secondary weapons
    */
   /* shooting secondary weapon */
   } else if (KEY("secondary") && NOHYP() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) {
         player_abortAutonav(NULL);
         player_setFlag(PLAYER_SECONDARY);
      }
      else if (value==KEY_RELEASE)
         player_rmFlag(PLAYER_SECONDARY);

   /* selecting secondary weapon */
   } else if (KEY("secondary_next") && INGAME() && NODEAD()) {
      if (value==KEY_PRESS) player_secondaryNext();
   } else if (KEY("secondary_prev") && INGAME() && NODEAD()) {
      if (value==KEY_PRESS) player_secondaryPrev();


   /*                                                                     
    * space
    */
   } else if (KEY("autonav") && INGAME() && NOHYP() && NODEAD()) {
      if (value==KEY_PRESS) player_startAutonav();
   /* target planet (cycles like target) */
   } else if (KEY("target_planet") && INGAME() && NOHYP() && NODEAD()) {
      if (value==KEY_PRESS) player_targetPlanet();
   /* target nearest planet or attempt to land */
   } else if (KEY("land") && INGAME() && NOHYP() && NODEAD()) {
      if (value==KEY_PRESS) {
         player_abortAutonav(NULL);
         player_land();
      }
   } else if (KEY("thyperspace") && NOHYP() && NOLAND() && NODEAD()) {
      if (value==KEY_PRESS) {
         player_abortAutonav(NULL);
         player_targetHyperspace();
      }
   } else if (KEY("starmap") && NOHYP() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) map_open();
   } else if (KEY("jump") && INGAME() && !repeat) {
      if (value==KEY_PRESS) {
         player_abortAutonav(NULL);
         player_jump();
      }


   /*
    * Communication.
    */
   } else if (KEY("log_up") && INGAME() && NODEAD()) {
      if (value==KEY_PRESS) {
         gui_messageScrollUp(5);
      }
   } else if (KEY("log_down") && INGAME() && NODEAD()) {
      if (value==KEY_PRESS) {
         gui_messageScrollDown(5);
      }
   } else if (KEY("hail") && INGAME() && NOHYP() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) {
         player_hail();
      }
   } else if (KEY("autohail") && INGAME() && NOHYP() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) {
         player_autohail();
      }


   /*
    * misc
    */
   /* zooming in */
   } else if (KEY("mapzoomin") && INGAME() && NODEAD()) {
      if (value==KEY_PRESS) gui_setRadarRel(-1);
   /* zooming out */
   } else if (KEY("mapzoomout") && INGAME() && NODEAD()) {
      if (value==KEY_PRESS) gui_setRadarRel(1);
   /* take a screenshot */
   } else if (KEY("screenshot")) {
      if (value==KEY_PRESS) player_screenshot();
   /* pause the games */
   } else if (KEY("pause") && !repeat) {
      if (value==KEY_PRESS) {
         if (!toolkit_isOpen()) {
            if (paused)
               unpause_game();
            else
               pause_game();
         }
      }
   /* toggle speed mode */
   } else if (KEY("speed") && !repeat) {
      if (value==KEY_PRESS) {
         if (dt_mod == 1.) pause_setSpeed(2.);
         else pause_setSpeed(1.);
      }
   /* opens a small menu */
   } else if (KEY("menu") && NODEAD() && !repeat) {
      if (value==KEY_PRESS) menu_small();
   
   /* shows pilot information */
   } else if (KEY("info") && NOHYP() && NODEAD() && !repeat) {
      if (value==KEY_PRESS) menu_info();

   /* Opens the Lua console. */
   } else if (KEY("console") && NODEAD() && !repeat) {
      if (value==KEY_PRESS) cli_open();
   }
}
#undef KEY


/*
 * events
 */
/* prototypes */
static void input_joyaxis( const SDLKey axis, const int value );
static void input_joyevent( const int event, const SDLKey button );
static void input_keyevent( const int event, const SDLKey key, const SDLMod mod, const int repeat );

/*
 * joystick
 */
/**
 * @brief Filters a joystick axis event.
 *    @param axis Axis generated by the event.
 *    @param value Value of the axis.
 */
static void input_joyaxis( const SDLKey axis, const int value )
{
   int i, k;
   for (i=0; strcmp(keybindNames[i],"end"); i++) {
      if (input_keybinds[i]->key == axis) {
         /* Positive axis keybinding. */
         if ((input_keybinds[i]->type == KEYBIND_JAXISPOS)
               && (value >= 0)) {
            k = (value > 0) ? KEY_PRESS : KEY_RELEASE;
            input_key( i, k, fabs(((double)value)/32767.), 0 );
         }

         /* Negative axis keybinding. */
         if ((input_keybinds[i]->type == KEYBIND_JAXISNEG)
               && (value <= 0)) {
            k = (value < 0) ? KEY_PRESS : KEY_RELEASE;
            input_key( i, k, fabs(((double)value)/32767.), 0 );
         }
      }
   }
}
/**
 * @brief Filters a joystick button event.
 *    @param event Event type (down/up).
 *    @param button Button generating the event.
 */
static void input_joyevent( const int event, const SDLKey button )
{
   int i;
   for (i=0; strcmp(keybindNames[i],"end"); i++)
      if ((input_keybinds[i]->type == KEYBIND_JBUTTON) &&
            (input_keybinds[i]->key == button))
         input_key(i, event, -1., 0);
}


/*
 * keyboard
 */
/**
 * @brief Filters a keyboard event.
 *    @param event Event type (down/up).
 *    @param key Key generating the event.
 *    @param mod Modifiers active when event was generated.
 */
static void input_keyevent( const int event, SDLKey key, const SDLMod mod, const int repeat )
{
   int i;
   SDLMod mod_filtered;

   /* Filter to "NAEV" modifiers. */
   mod_filtered = input_translateMod(mod);

   for (i=0; strcmp(keybindNames[i],"end"); i++) {
      if ((input_keybinds[i]->type == KEYBIND_KEYBOARD) &&
            (input_keybinds[i]->key == key)) {
         if ((input_keybinds[i]->mod == mod_filtered) ||
               (input_keybinds[i]->mod == NMOD_ALL) ||
               (event == KEY_RELEASE)) /**< Release always gets through. */
            input_key(i, event, -1., repeat);
            /* No break so all keys get pressed if needed. */
      }
   }
}


/**
 * @brief Handles global input.
 *
 * Basically seperates the event types
 *
 *    @param event Incoming SDL_Event.
 */
void input_handle( SDL_Event* event )
{
   if (toolkit_isOpen()) /* toolkit handled seperately completely */
      if (toolkit_input(event))
         return; /* we don't process it if toolkit grabs it */

   switch (event->type) {

      /*
       * game itself
       */
      case SDL_JOYAXISMOTION:
         input_joyaxis(event->jaxis.axis, event->jaxis.value);
         break;

      case SDL_JOYBUTTONDOWN:
         input_joyevent(KEY_PRESS, event->jbutton.button);
         break;

      case SDL_JOYBUTTONUP:
         input_joyevent(KEY_RELEASE, event->jbutton.button);
         break;

      case SDL_KEYDOWN:
         input_keyevent(KEY_PRESS, event->key.keysym.sym, event->key.keysym.mod, 0);
         break;

      case SDL_KEYUP:
         input_keyevent(KEY_RELEASE, event->key.keysym.sym, event->key.keysym.mod, 0);
         break;
   }
}

