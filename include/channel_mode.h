/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 1997-2017 ircd-hybrid development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 *  USA
 */

/*! \file channel_mode.h
 * \brief Includes channel mode related definitions and structures.
 * \version $Id$
 */

#ifndef INCLUDED_channel_mode_h
#define INCLUDED_channel_mode_h


#define CMEMBER_STATUS_FLAGS "@%+"
enum { CMEMBER_STATUS_FLAGS_LEN = sizeof(CMEMBER_STATUS_FLAGS) - 1 };

enum { MODEBUFLEN = 200 };

/* Maximum mode changes allowed per client, per server is different */
enum { MAXMODEPARAMS = 6 };

enum
{
  MODE_QUERY = 0,
  MODE_DEL   = 1,
  MODE_ADD   = 2
};

enum
{
  CHACCESS_NOTONCHAN = -1,
  CHACCESS_PEON      =  0,
  CHACCESS_HALFOP    =  1,
  CHACCESS_CHANOP    =  2,
  CHACCESS_REMOTE    =  3
};

/* can_send results */
enum
{
  CAN_SEND_NO    =  0,
  CAN_SEND_NONOP = -1,
  CAN_SEND_OPV   = -2
};

/* Channel related flags */
enum
{
  CHFL_CHANOP       = 0x00000001U,  /* Channel operator   */
  CHFL_HALFOP       = 0x00000002U,  /* Channel half op    */
  CHFL_VOICE        = 0x00000004U,  /* the power to speak */
  CHFL_BAN          = 0x00000008U,  /* ban channel flag */
  CHFL_EXCEPTION    = 0x00000010U,  /* exception to ban channel flag */
  CHFL_INVEX        = 0x00000020U,
  /* Cache flags for silence on ban */
  CHFL_BAN_CHECKED  = 0x00000040U,
  CHFL_BAN_SILENCED = 0x00000080U
};

/* channel modes ONLY */
enum
{
  MODE_PRIVATE    = 0x00000001U,  /**< */
  MODE_SECRET     = 0x00000002U,  /**< Channel does not show up on NAMES or LIST */
  MODE_MODERATED  = 0x00000004U,  /**< Users without +v/+h/+o cannot send text to the channel */
  MODE_TOPICLIMIT = 0x00000008U,  /**< Only chanops can change the topic */
  MODE_INVITEONLY = 0x00000010U,  /**< Only invited users may join this channel */
  MODE_NOPRIVMSGS = 0x00000020U,  /**< Users must be in the channel to send text to it */
  MODE_SSLONLY    = 0x00000040U,  /**< Prevents anyone who isn't connected via SSL/TLS from joining the channel */
  MODE_OPERONLY   = 0x00000080U,  /**< Prevents anyone who hasn't obtained IRC operator status from joining the channel */
  MODE_REGISTERED = 0x00000100U,  /**< Channel has been registered with ChanServ */
  MODE_REGONLY    = 0x00000200U,  /**< Only registered clients may join a channel with that mode set */
  MODE_NOCTRL     = 0x00000400U,  /**< Prevents users from sending messages containing control codes to the channel */
  MODE_MODREG     = 0x00000800U,  /**< Unregistered/unidentified clients cannot send text to the channel */
  MODE_NOCTCP     = 0x00001000U,  /**< Clients cannot send CTCP messages to the channel */
  MODE_NONOTICE   = 0x00002000U,  /**< Clients cannot send NOTICE to the channel */
  MODE_HIDEBMASKS = 0x00004000U,  /**< Hides +b/+e/+I lists/changes for non-chanops everywhere */
  MODE_EXTLIMIT   = 0x00008000U   /**< Channel can make use of the extended ban list limit */
};

#define HasCMode(x, y) ((x)->mode.mode &   (y))
#define AddCMode(x, y) ((x)->mode.mode |=  (y))
#define DelCMode(x, y) ((x)->mode.mode &= ~(y))

/* name invisible */
#define SecretChannel(x)        (((x)->mode.mode & MODE_SECRET))
#define PubChannel(x)           (((x)->mode.mode & (MODE_PRIVATE | MODE_SECRET)) == 0)
/* knock is forbidden, halfops can't kick/deop other halfops. */
#define PrivateChannel(x)       (((x)->mode.mode & MODE_PRIVATE))

struct ChModeChange
{
  char letter;
  const char *arg;
  const char *id;
  unsigned int dir;
  unsigned int flags;
};

struct chan_mode
{
  const unsigned char letter;
  const unsigned int mode;
  const unsigned int only_opers;
  const unsigned int only_servers;
  void (*func)(struct Client *,
               struct Channel *, int, int *, char **, int *,
               int, int, const char, const struct chan_mode *);
};

extern const struct chan_mode *cmode_map[];
extern const struct chan_mode  cmode_tab[];

extern void channel_mode_init(void);
extern int add_id(struct Client *, struct Channel *, char *, unsigned int);
extern void set_channel_mode(struct Client *, struct Channel *,
                             struct Membership *, int, char **);
extern void clear_ban_cache_list(dlink_list *);
#endif /* INCLUDED_channel_mode_h */
