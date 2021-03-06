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

/*! \file channel_mode.c
 * \brief Controls modes on channels.
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "channel.h"
#include "channel_mode.h"
#include "client.h"
#include "conf.h"
#include "hostmask.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "server.h"
#include "send.h"
#include "memory.h"
#include "mempool.h"
#include "parse.h"


static char nuh_mask[MAXPARA][IRCD_BUFSIZE];
static struct ChModeChange mode_changes[IRCD_BUFSIZE];
static unsigned int mode_count;
static unsigned int mode_limit;  /* number of modes set other than simple */
static unsigned int simple_modes_mask;  /* bit mask of simple modes already set */
extern mp_pool_t *ban_pool;


/* check_string()
 *
 * inputs       - string to check
 * output       - pointer to modified string
 * side effects - Fixes a string so that the first white space found
 *                becomes an end of string marker (`\0`).
 *                returns the 'fixed' string or "*" if the string
 *                was NULL length or a NULL pointer.
 */
static char *
check_string(char *s)
{
  char *const str = s;
  static char star[] = "*";

  if (EmptyString(str))
    return star;

  for (; *s; ++s)
  {
    if (IsSpace(*s))
    {
      *s = '\0';
      break;
    }
  }

  return EmptyString(str) ? star : str;
}

/*
 * Ban functions to work with mode +b/e/I
 */
/* add the specified ID to the channel.
 *   -is 8/9/00
 */

int
add_id(struct Client *client_p, struct Channel *chptr, char *banid, unsigned int type)
{
  dlink_list *list;
  dlink_node *node;
  char name[NICKLEN + 1] = "";
  char user[USERLEN + 1] = "";
  char host[HOSTLEN + 1] = "";
  struct split_nuh_item nuh;

  if (MyClient(client_p))
  {
    unsigned int num_mask = dlink_list_length(&chptr->banlist) +
                            dlink_list_length(&chptr->exceptlist) +
                            dlink_list_length(&chptr->invexlist);

    /* Don't let local clients overflow the b/e/I lists */
    if (num_mask >= ((HasCMode(chptr, MODE_EXTLIMIT)) ? ConfigChannel.max_bans_large :
                                                        ConfigChannel.max_bans))
    {
      sendto_one_numeric(client_p, &me, ERR_BANLISTFULL, chptr->name, banid);
      return 0;
    }

    collapse(banid);
  }

  nuh.nuhmask  = check_string(banid);
  nuh.nickptr  = name;
  nuh.userptr  = user;
  nuh.hostptr  = host;

  nuh.nicksize = sizeof(name);
  nuh.usersize = sizeof(user);
  nuh.hostsize = sizeof(host);

  split_nuh(&nuh);

  /*
   * Re-assemble a new n!u@h and print it back to banid for sending
   * the mode to the channel.
   */
  size_t len = snprintf(banid, IRCD_BUFSIZE, "%s!%s@%s", name, user, host);

  switch (type)
  {
    case CHFL_BAN:
      list = &chptr->banlist;
      clear_ban_cache_list(&chptr->locmembers);
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      clear_ban_cache_list(&chptr->locmembers);
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      list = NULL;  /* Let it crash */
  }

  DLINK_FOREACH(node, list->head)
  {
    const struct Ban *ban = node->data;

    if (!irccmp(ban->name, name) &&
        !irccmp(ban->user, user) &&
        !irccmp(ban->host, host))
      return 0;
  }

  struct Ban *ban = mp_pool_get(ban_pool);
  ban->when = CurrentTime;
  ban->len = len - 2;  /* -2 for ! + @ */
  ban->type = parse_netmask(host, &ban->addr, &ban->bits);
  strlcpy(ban->name, name, sizeof(ban->name));
  strlcpy(ban->user, user, sizeof(ban->user));
  strlcpy(ban->host, host, sizeof(ban->host));

  if (IsClient(client_p))
    snprintf(ban->who, sizeof(ban->who), "%s!%s@%s", client_p->name,
             client_p->username, client_p->host);
  else if (IsHidden(client_p) || ConfigServerHide.hide_servers)
    strlcpy(ban->who, me.name, sizeof(ban->who));
  else
    strlcpy(ban->who, client_p->name, sizeof(ban->who));

  dlinkAdd(ban, &ban->node, list);

  return 1;
}

/*
 * inputs	- pointer to channel
 *		- pointer to ban id
 *		- type of ban, i.e. ban, exception, invex
 * output	- 0 for failure, 1 for success
 * side effects	-
 */
static int
del_id(struct Channel *chptr, char *banid, unsigned int type)
{
  dlink_list *list;
  dlink_node *node;
  char name[NICKLEN + 1] = "";
  char user[USERLEN + 1] = "";
  char host[HOSTLEN + 1] = "";
  struct split_nuh_item nuh;

  assert(banid);

  nuh.nuhmask  = check_string(banid);
  nuh.nickptr  = name;
  nuh.userptr  = user;
  nuh.hostptr  = host;

  nuh.nicksize = sizeof(name);
  nuh.usersize = sizeof(user);
  nuh.hostsize = sizeof(host);

  split_nuh(&nuh);

  /*
   * Re-assemble a new n!u@h and print it back to banid for sending
   * the mode to the channel.
   */
  snprintf(banid, IRCD_BUFSIZE, "%s!%s@%s", name, user, host);

  switch (type)
  {
    case CHFL_BAN:
      list = &chptr->banlist;
      clear_ban_cache_list(&chptr->locmembers);
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      clear_ban_cache_list(&chptr->locmembers);
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      list = NULL;  /* Let it crash */
  }

  DLINK_FOREACH(node, list->head)
  {
    struct Ban *ban = node->data;

    if (!irccmp(name, ban->name) &&
        !irccmp(user, ban->user) &&
        !irccmp(host, ban->host))
    {
      remove_ban(ban, list);
      return 1;
    }
  }

  return 0;
}

/* channel_modes()
 *
 * inputs       - pointer to channel
 *              - pointer to client
 *              - pointer to mode buf
 *              - pointer to parameter buf
 * output       - NONE
 * side effects - write the "simple" list of channel modes for channel
 * chptr onto buffer mbuf with the parameters in pbuf.
 */
void
channel_modes(struct Channel *chptr, struct Client *client_p, char *mbuf, char *pbuf)
{
  *mbuf++ = '+';
  *pbuf = '\0';

  for (const struct chan_mode *tab = cmode_tab; tab->letter; ++tab)
    if (tab->mode && HasCMode(chptr, tab->mode))
      *mbuf++ = tab->letter;

  if (chptr->mode.limit)
  {
    *mbuf++ = 'l';

    if (IsServer(client_p) || IsMember(client_p, chptr))
      pbuf += sprintf(pbuf, "%u ", chptr->mode.limit);
  }

  if (chptr->mode.key[0])
  {
    *mbuf++ = 'k';

    if (IsServer(client_p) || IsMember(client_p, chptr))
      sprintf(pbuf, "%s ", chptr->mode.key);
  }

  *mbuf = '\0';
}

/* fix_key()
 *
 * inputs       - pointer to key to clean up
 * output       - pointer to cleaned up key
 * side effects - input string is modified
 *
 * stolen from Undernet's ircd  -orabidoo
 */
static char *
fix_key(char *arg)
{
  unsigned char *s, *t, c;

  for (s = t = (unsigned char *)arg; (c = *s) && s - (unsigned char *)arg < KEYLEN; ++s)
  {
    c &= 0x7f;

    if (c != ':' && c > ' ' && c != ',')
      *t++ = c;
  }

  *t = '\0';
  return arg;
}

/*
 * inputs       - pointer to channel
 * output       - none
 * side effects - clear ban cache
 */
void
clear_ban_cache_list(dlink_list *list)
{
  dlink_node *node;

  DLINK_FOREACH(node, list->head)
  {
    struct Membership *member = node->data;
    member->flags &= ~(CHFL_BAN_SILENCED | CHFL_BAN_CHECKED);
  }
}

/*
 * Bitmasks for various error returns that set_channel_mode should only return
 * once per call  -orabidoo
 */
enum
{
  SM_ERR_NOOPS        = 1 << 0,  /* No chan ops */
  SM_ERR_UNKNOWN      = 1 << 1,
  SM_ERR_RPL_B        = 1 << 2,
  SM_ERR_RPL_E        = 1 << 3,
  SM_ERR_RPL_I        = 1 << 4,
  SM_ERR_NOTONCHANNEL = 1 << 5,  /* Client is not on channel */
  SM_ERR_NOTOPER      = 1 << 6,  /* Only irc-operators can change that mode */
  SM_ERR_ONLYSERVER   = 1 << 7   /* Only servers or services can change that mode */
};

/* Mode functions handle mode changes for a particular mode... */
static void
chm_nosuch(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
           int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  if (*errors & SM_ERR_UNKNOWN)
    return;

  *errors |= SM_ERR_UNKNOWN;
  sendto_one_numeric(source_p, &me, ERR_UNKNOWNMODE, c);
}

static void
chm_simple(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
           int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  if (mode->only_opers)
  {
    if (MyClient(source_p) && !HasUMode(source_p, UMODE_OPER))
    {
      if (!(*errors & SM_ERR_NOTOPER))
        sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);

      *errors |= SM_ERR_NOTOPER;
      return;
    }
  }

  if (mode->only_servers)
  {
    if (!IsServer(source_p) && !HasFlag(source_p, FLAGS_SERVICE))
    {
      if (!(*errors & SM_ERR_ONLYSERVER))
        sendto_one_numeric(source_p, &me,
                           alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                           ERR_ONLYSERVERSCANCHANGE, chptr->name);

      *errors |= SM_ERR_ONLYSERVER;
      return;
    }
  }

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);

    *errors |= SM_ERR_NOOPS;
    return;
  }

  /* If have already dealt with this simple mode, ignore it */
  if (simple_modes_mask & mode->mode)
    return;

  simple_modes_mask |= mode->mode;

  if (dir == MODE_ADD)  /* setting + */
  {
    if (MyClient(source_p) && HasCMode(chptr, mode->mode))
      return;

    AddCMode(chptr, mode->mode);
  }
  else if (dir == MODE_DEL)  /* setting - */
  {
    if (MyClient(source_p) && !HasCMode(chptr, mode->mode))
      return;

    DelCMode(chptr, mode->mode);
  }

  mode_changes[mode_count].letter = mode->letter;
  mode_changes[mode_count].arg = NULL;
  mode_changes[mode_count].id = NULL;
  mode_changes[mode_count].flags = 0;
  mode_changes[mode_count++].dir = dir;
}

static void
chm_ban(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
        int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  if (dir == MODE_QUERY || parc <= *parn)
  {
    dlink_node *node;

    if (*errors & SM_ERR_RPL_B)
      return;

    *errors |= SM_ERR_RPL_B;

    DLINK_FOREACH(node, chptr->banlist.head)
    {
      const struct Ban *ban = node->data;

      if (!HasCMode(chptr, MODE_HIDEBMASKS) || alev >= CHACCESS_HALFOP)
        sendto_one_numeric(source_p, &me, RPL_BANLIST, chptr->name,
                           ban->name, ban->user, ban->host,
                           ban->who, ban->when);
    }

    sendto_one_numeric(source_p, &me, RPL_ENDOFBANLIST, chptr->name);
    return;
  }

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);

    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
    return;

  char *const mask = nuh_mask[*parn];
  strlcpy(mask, parv[*parn], sizeof(nuh_mask[*parn]));
  ++(*parn);

  if (*mask == ':' || (!MyConnect(source_p) && strchr(mask, ' ')))
    return;

  if (dir == MODE_ADD)  /* setting + */
  {
    if (!add_id(source_p, chptr, mask, CHFL_BAN))
      return;
  }
  else if (dir == MODE_DEL)  /* setting - */
  {
    if (!del_id(chptr, mask, CHFL_BAN))
      return;
  }

  mode_changes[mode_count].letter = mode->letter;
  mode_changes[mode_count].arg = mask;  /* At this point 'mask' is no longer than NICKLEN + USERLEN + HOSTLEN + 3 */
  mode_changes[mode_count].id = NULL;
  if (HasCMode(chptr, MODE_HIDEBMASKS))
    mode_changes[mode_count].flags = CHFL_CHANOP | CHFL_HALFOP;
  else
    mode_changes[mode_count].flags = 0;
  mode_changes[mode_count++].dir = dir;
}

static void
chm_except(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
           int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  if (dir == MODE_QUERY || parc <= *parn)
  {
    dlink_node *node;

    if (*errors & SM_ERR_RPL_E)
      return;

    *errors |= SM_ERR_RPL_E;

    DLINK_FOREACH(node, chptr->exceptlist.head)
    {
      const struct Ban *ban = node->data;

      if (!HasCMode(chptr, MODE_HIDEBMASKS) || alev >= CHACCESS_HALFOP)
        sendto_one_numeric(source_p, &me, RPL_EXCEPTLIST, chptr->name,
                           ban->name, ban->user, ban->host,
                           ban->who, ban->when);
    }

    sendto_one_numeric(source_p, &me, RPL_ENDOFEXCEPTLIST, chptr->name);
    return;
  }

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);

    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
    return;

  char *const mask = nuh_mask[*parn];
  strlcpy(mask, parv[*parn], sizeof(nuh_mask[*parn]));
  ++(*parn);

  if (*mask == ':' || (!MyConnect(source_p) && strchr(mask, ' ')))
    return;

  if (dir == MODE_ADD)  /* setting + */
  {
    if (!add_id(source_p, chptr, mask, CHFL_EXCEPTION))
      return;
  }
  else if (dir == MODE_DEL)  /* setting - */
  {
    if (!del_id(chptr, mask, CHFL_EXCEPTION))
      return;
  }

  mode_changes[mode_count].letter = mode->letter;
  mode_changes[mode_count].arg = mask;  /* At this point 'mask' is no longer than NICKLEN + USERLEN + HOSTLEN + 3 */
  mode_changes[mode_count].id = NULL;
  if (HasCMode(chptr, MODE_HIDEBMASKS))
    mode_changes[mode_count].flags = CHFL_CHANOP | CHFL_HALFOP;
  else
    mode_changes[mode_count].flags = 0;
  mode_changes[mode_count++].dir = dir;
}

static void
chm_invex(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
          int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  if (dir == MODE_QUERY || parc <= *parn)
  {
    dlink_node *node;

    if (*errors & SM_ERR_RPL_I)
      return;

    *errors |= SM_ERR_RPL_I;

    DLINK_FOREACH(node, chptr->invexlist.head)
    {
      const struct Ban *ban = node->data;

      if (!HasCMode(chptr, MODE_HIDEBMASKS) || alev >= CHACCESS_HALFOP)
        sendto_one_numeric(source_p, &me, RPL_INVEXLIST, chptr->name,
                           ban->name, ban->user, ban->host,
                           ban->who, ban->when);
    }

    sendto_one_numeric(source_p, &me, RPL_ENDOFINVEXLIST, chptr->name);
    return;
  }

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);

    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
    return;

  char *const mask = nuh_mask[*parn];
  strlcpy(mask, parv[*parn], sizeof(nuh_mask[*parn]));
  ++(*parn);

  if (*mask == ':' || (!MyConnect(source_p) && strchr(mask, ' ')))
    return;

  if (dir == MODE_ADD)  /* setting + */
  {
    if (!add_id(source_p, chptr, mask, CHFL_INVEX))
      return;
  }
  else if (dir == MODE_DEL)  /* setting - */
  {
    if (!del_id(chptr, mask, CHFL_INVEX))
      return;
  }

  mode_changes[mode_count].letter = mode->letter;
  mode_changes[mode_count].arg = mask;  /* At this point 'mask' is no longer than NICKLEN + USERLEN + HOSTLEN + 3 */
  mode_changes[mode_count].id = NULL;
  if (HasCMode(chptr, MODE_HIDEBMASKS))
    mode_changes[mode_count].flags = CHFL_CHANOP | CHFL_HALFOP;
  else
    mode_changes[mode_count].flags = 0;
  mode_changes[mode_count++].dir = dir;
}

static void
chm_voice(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
          int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  struct Client *target_p;
  struct Membership *member;

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);

    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == MODE_QUERY || parc <= *parn)
    return;

  if ((target_p = find_chasing(source_p, parv[(*parn)++])) == NULL)
    return;  /* find_chasing sends ERR_NOSUCHNICK */

  if ((member = find_channel_link(target_p, chptr)) == NULL)
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one_numeric(source_p, &me, ERR_USERNOTINCHANNEL, target_p->name, chptr->name);

    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if (MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
    return;

  if (dir == MODE_ADD)  /* setting + */
  {
    if (has_member_flags(member, CHFL_VOICE))
      return;  /* No redundant mode changes */

    AddMemberFlag(member, CHFL_VOICE);
  }
  else if (dir == MODE_DEL)  /* setting - */
  {
    if (!has_member_flags(member, CHFL_VOICE))
      return;  /* No redundant mode changes */

    DelMemberFlag(member, CHFL_VOICE);
  }

  mode_changes[mode_count].letter = mode->letter;
  mode_changes[mode_count].arg = target_p->name;
  mode_changes[mode_count].id = target_p->id;
  mode_changes[mode_count].flags = 0;
  mode_changes[mode_count++].dir = dir;
}

static void
chm_hop(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
        int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  struct Client *target_p;
  struct Membership *member;

  if (alev < CHACCESS_CHANOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);

    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == MODE_QUERY || parc <= *parn)
    return;

  if ((target_p = find_chasing(source_p, parv[(*parn)++])) == NULL)
    return;  /* find_chasing sends ERR_NOSUCHNICK */

  if ((member = find_channel_link(target_p, chptr)) == NULL)
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one_numeric(source_p, &me, ERR_USERNOTINCHANNEL, target_p->name, chptr->name);

    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if (MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
    return;

  if (dir == MODE_ADD)  /* setting + */
  {
    if (has_member_flags(member, CHFL_HALFOP))
      return;  /* No redundant mode changes */

    AddMemberFlag(member, CHFL_HALFOP);
  }
  else if (dir == MODE_DEL)  /* setting - */
  {
    if (!has_member_flags(member, CHFL_HALFOP))
      return;  /* No redundant mode changes */

    DelMemberFlag(member, CHFL_HALFOP);
  }

  mode_changes[mode_count].letter = mode->letter;
  mode_changes[mode_count].arg = target_p->name;
  mode_changes[mode_count].id = target_p->id;
  mode_changes[mode_count].flags = 0;
  mode_changes[mode_count++].dir = dir;
}

static void
chm_op(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
       int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  struct Client *target_p;
  struct Membership *member;

  if (alev < CHACCESS_CHANOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);

    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == MODE_QUERY || parc <= *parn)
    return;

  if ((target_p = find_chasing(source_p, parv[(*parn)++])) == NULL)
    return;  /* find_chasing sends ERR_NOSUCHNICK */

  if ((member = find_channel_link(target_p, chptr)) == NULL)
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one_numeric(source_p, &me, ERR_USERNOTINCHANNEL, target_p->name, chptr->name);

    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if (MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
    return;

  if (dir == MODE_ADD)  /* setting + */
  {
    if (has_member_flags(member, CHFL_CHANOP))
      return;  /* No redundant mode changes */

    AddMemberFlag(member, CHFL_CHANOP);
  }
  else if (dir == MODE_DEL)  /* setting - */
  {
    if (!has_member_flags(member, CHFL_CHANOP))
      return;  /* No redundant mode changes */

    DelMemberFlag(member, CHFL_CHANOP);
  }

  mode_changes[mode_count].letter = mode->letter;
  mode_changes[mode_count].arg = target_p->name;
  mode_changes[mode_count].id = target_p->id;
  mode_changes[mode_count].flags = 0;
  mode_changes[mode_count++].dir = dir;
}

static void
chm_limit(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
          int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == MODE_QUERY)
    return;

  if (dir == MODE_ADD && parc > *parn)
  {
    char *const lstr = parv[(*parn)++];
    int limit = 0;

    if (EmptyString(lstr) || (limit = atoi(lstr)) <= 0)
      return;

    sprintf(lstr, "%d", limit);

    /* If somebody sets MODE #channel +ll 1 2, accept latter --fl */
    for (unsigned int i = 0; i < mode_count; ++i)
      if (mode_changes[i].letter == mode->letter && mode_changes[i].dir == MODE_ADD)
        mode_changes[i].letter = 0;

    mode_changes[mode_count].letter = mode->letter;
    mode_changes[mode_count].arg = lstr;
    mode_changes[mode_count].id = NULL;
    mode_changes[mode_count].flags = 0;
    mode_changes[mode_count++].dir = dir;

    chptr->mode.limit = limit;
  }
  else if (dir == MODE_DEL)
  {
    if (!chptr->mode.limit)
      return;

    chptr->mode.limit = 0;

    mode_changes[mode_count].letter = mode->letter;
    mode_changes[mode_count].arg = NULL;
    mode_changes[mode_count].id = NULL;
    mode_changes[mode_count].flags = 0;
    mode_changes[mode_count++].dir = dir;
  }
}

static void
chm_key(struct Client *source_p, struct Channel *chptr, int parc, int *parn, char **parv,
        int *errors, int alev, int dir, const char c, const struct chan_mode *mode)
{
  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one_numeric(source_p, &me,
                         alev == CHACCESS_NOTONCHAN ? ERR_NOTONCHANNEL :
                         ERR_CHANOPRIVSNEEDED, chptr->name);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == MODE_QUERY)
    return;

  if (dir == MODE_ADD && parc > *parn)
  {
    char *const key = fix_key(parv[(*parn)++]);

    if (EmptyString(key))
      return;

    assert(key[0] != ' ');
    strlcpy(chptr->mode.key, key, sizeof(chptr->mode.key));

    /* If somebody does MODE #channel +kk a b, accept latter --fl */
    for (unsigned int i = 0; i < mode_count; ++i)
      if (mode_changes[i].letter == mode->letter && mode_changes[i].dir == MODE_ADD)
        mode_changes[i].letter = 0;

    mode_changes[mode_count].letter = mode->letter;
    mode_changes[mode_count].arg = key;
    mode_changes[mode_count].id = NULL;
    mode_changes[mode_count].flags = 0;
    mode_changes[mode_count++].dir = dir;
  }
  else if (dir == MODE_DEL)
  {
    if (parc > *parn)
      ++(*parn);

    if (chptr->mode.key[0] == '\0')
      return;

    chptr->mode.key[0] = '\0';

    mode_changes[mode_count].letter = mode->letter;
    mode_changes[mode_count].arg = "*";
    mode_changes[mode_count].id = NULL;
    mode_changes[mode_count].flags = 0;
    mode_changes[mode_count++].dir = dir;
  }
}

/* get_channel_access()
 *
 * inputs       - pointer to Client struct
 *              - pointer to Membership struct
 * output       - CHACCESS_CHANOP if we should let them have
 *                chanop level access, 0 for peon level access.
 * side effects - NONE
 */
static int
get_channel_access(const struct Client *source_p,
                   const struct Membership *member)
{
  /* Let hacked servers in for now... */
  if (!MyClient(source_p))
    return CHACCESS_REMOTE;

  if (!member)
    return CHACCESS_NOTONCHAN;

  /* Just to be sure.. */
  assert(source_p == member->client_p);

  if (has_member_flags(member, CHFL_CHANOP))
    return CHACCESS_CHANOP;

  if (has_member_flags(member, CHFL_HALFOP))
    return CHACCESS_HALFOP;

  return CHACCESS_PEON;
}

/* send_mode_changes_server()
 * Input: the source client(source_p),
 *        the channel to send mode changes for(chptr)
 * Output: None.
 * Side-effects: Sends the appropriate mode changes to servers.
 *
 */
static void
send_mode_changes_server(struct Client *source_p, struct Channel *chptr)
{
  char modebuf[IRCD_BUFSIZE] = "";
  char parabuf[IRCD_BUFSIZE] = "";
  char *parptr = parabuf;
  unsigned int mbl = 0, pbl = 0, arglen = 0, modecount = 0, paracount = 0;
  unsigned int dir = MODE_QUERY;

  mbl = snprintf(modebuf, sizeof(modebuf), ":%s TMODE %ju %s ",
                 source_p->id, chptr->creationtime, chptr->name);

  /* Loop the list of modes we have */
  for (unsigned int i = 0; i < mode_count; ++i)
  {
    if (mode_changes[i].letter == 0)
      continue;

    const char *arg;
    if (mode_changes[i].id)
      arg = mode_changes[i].id;
    else
      arg = mode_changes[i].arg;

    if (arg)
      arglen = strlen(arg);
    else
      arglen = 0;

    /*
     * If we're creeping past the buf size, we need to send it and make
     * another line for the other modes
     */
    if ((paracount == MAXMODEPARAMS) ||
        ((arglen + mbl + pbl + 2 /* +2 for /r/n */ ) > IRCD_BUFSIZE))
    {
      if (modecount)
        sendto_server(source_p, 0, 0, "%s %s", modebuf, parabuf);

      modecount = 0;
      paracount = 0;

      mbl = snprintf(modebuf, sizeof(modebuf), ":%s TMODE %ju %s ",
                     source_p->id, chptr->creationtime, chptr->name);

      pbl = 0;
      parabuf[0] = '\0';
      parptr = parabuf;
      dir = MODE_QUERY;
    }

    if (dir != mode_changes[i].dir)
    {
      modebuf[mbl++] = (mode_changes[i].dir == MODE_ADD) ? '+' : '-';
      dir = mode_changes[i].dir;
    }

    modebuf[mbl++] = mode_changes[i].letter;
    modebuf[mbl] = '\0';
    ++modecount;

    if (arg)
    {
      int len = sprintf(parptr, (pbl == 0) ? "%s" : " %s", arg);
      pbl += len;
      parptr += len;
      ++paracount;
    }
  }

  if (modecount)
    sendto_server(source_p, 0, 0, "%s %s", modebuf, parabuf);
}

/* void send_mode_changes(struct Client *client_p,
 *                        struct Client *source_p,
 *                        struct Channel *chptr)
 * Input: The client sending(client_p), the source client(source_p),
 *        the channel to send mode changes for(chptr),
 *        mode change globals.
 * Output: None.
 * Side-effects: Sends the appropriate mode changes to other clients
 *               and propagates to servers.
 */
static void
send_mode_changes_client(struct Client *source_p, struct Channel *chptr)
{
  unsigned int flags = 0;

  for (unsigned int pass = 2; pass--; flags = CHFL_CHANOP | CHFL_HALFOP)
  {
    char modebuf[IRCD_BUFSIZE] = "";
    char parabuf[IRCD_BUFSIZE] = "";
    char *parptr = parabuf;
    unsigned int mbl = 0, pbl = 0, arglen = 0, modecount = 0, paracount = 0;
    unsigned int dir = MODE_QUERY;

    if (IsServer(source_p))
      mbl = snprintf(modebuf, sizeof(modebuf), ":%s MODE %s ", (IsHidden(source_p) ||
                     ConfigServerHide.hide_servers) ?
                     me.name : source_p->name, chptr->name);
    else
      mbl = snprintf(modebuf, sizeof(modebuf), ":%s!%s@%s MODE %s ", source_p->name,
                     source_p->username, source_p->host, chptr->name);

    for (unsigned int i = 0; i < mode_count; ++i)
    {
      if (mode_changes[i].letter == 0 || mode_changes[i].flags != flags)
        continue;

      const char *arg = mode_changes[i].arg;
      if (arg)
        arglen = strlen(arg);
      else
        arglen = 0;

      if ((paracount == MAXMODEPARAMS) ||
          ((arglen + mbl + pbl + 2 /* +2 for /r/n */ ) > IRCD_BUFSIZE))
      {
        if (modecount)
          sendto_channel_local(NULL, chptr, flags, 0, 0, "%s %s", modebuf, parabuf);

        modecount = 0;
        paracount = 0;

        if (IsServer(source_p))
          mbl = snprintf(modebuf, sizeof(modebuf), ":%s MODE %s ", (IsHidden(source_p) ||
                         ConfigServerHide.hide_servers) ?
                         me.name : source_p->name, chptr->name);
        else
          mbl = snprintf(modebuf, sizeof(modebuf), ":%s!%s@%s MODE %s ", source_p->name,
                         source_p->username, source_p->host, chptr->name);

        pbl = 0;
        parabuf[0] = '\0';
        parptr = parabuf;
        dir = MODE_QUERY;
      }

      if (dir != mode_changes[i].dir)
      {
        modebuf[mbl++] = (mode_changes[i].dir == MODE_ADD) ? '+' : '-';
        dir = mode_changes[i].dir;
      }

      modebuf[mbl++] = mode_changes[i].letter;
      modebuf[mbl] = '\0';
      ++modecount;

      if (arg)
      {
        int len = sprintf(parptr, (pbl == 0) ? "%s" : " %s", arg);
        pbl += len;
        parptr += len;
        ++paracount;
      }
    }

    if (modecount)
      sendto_channel_local(NULL, chptr, flags, 0, 0, "%s %s", modebuf, parabuf);
  }
}

const struct chan_mode *cmode_map[256];
const struct chan_mode  cmode_tab[] =
{
  { .letter = 'b', .func = chm_ban },
  { .letter = 'c', .mode = MODE_NOCTRL, .func = chm_simple },
  { .letter = 'e', .func = chm_except },
  { .letter = 'h', .func = chm_hop },
  { .letter = 'i', .mode = MODE_INVITEONLY, .func = chm_simple },
  { .letter = 'k', .func = chm_key },
  { .letter = 'l', .func = chm_limit },
  { .letter = 'm', .mode = MODE_MODERATED, .func = chm_simple },
  { .letter = 'n', .mode = MODE_NOPRIVMSGS, .func = chm_simple },
  { .letter = 'o', .func = chm_op },
  { .letter = 'p', .mode = MODE_PRIVATE, .func = chm_simple },
  { .letter = 'r', .mode = MODE_REGISTERED, .only_servers = 1, .func = chm_simple },
  { .letter = 's', .mode = MODE_SECRET, .func = chm_simple },
  { .letter = 't', .mode = MODE_TOPICLIMIT, .func = chm_simple },
  { .letter = 'u', .mode = MODE_HIDEBMASKS, .func = chm_simple },
  { .letter = 'v', .func = chm_voice },
  { .letter = 'C', .mode = MODE_NOCTCP, .func = chm_simple },
  { .letter = 'I', .func = chm_invex },
  { .letter = 'L', .mode = MODE_EXTLIMIT, .only_opers = 1, .func = chm_simple },
  { .letter = 'M', .mode = MODE_MODREG, .func = chm_simple },
  { .letter = 'O', .mode = MODE_OPERONLY, .only_opers = 1, .func = chm_simple },
  { .letter = 'R', .mode = MODE_REGONLY, .func = chm_simple },
  { .letter = 'S', .mode = MODE_SSLONLY, .func = chm_simple },
  { .letter = 'T', .mode = MODE_NONOTICE, .func = chm_simple },
  { .letter = '\0', .mode = 0 }
};

void
channel_mode_init(void)
{
  for (const struct chan_mode *tab = cmode_tab; tab->letter; ++tab)
    cmode_map[tab->letter] = tab;
}

/*
 * Input: The the client this originated
 *        from, the channel, the parameter count starting at the modes,
 *        the parameters, the channel name.
 * Output: None.
 * Side-effects: Changes the channel membership and modes appropriately,
 *               sends the appropriate MODE messages to the appropriate
 *               clients.
 */
void
set_channel_mode(struct Client *source_p, struct Channel *chptr,
                 struct Membership *member, int parc, char *parv[])
{
  int dir = MODE_ADD;
  int parn = 1;
  int alevel = 0, errors = 0;

  mode_count = 0;
  mode_limit = 0;
  simple_modes_mask = 0;

  alevel = get_channel_access(source_p, member);

  for (const char *ml = parv[0]; *ml; ++ml)
  {
    switch (*ml)
    {
      case '+':
        dir = MODE_ADD;
        break;
      case '-':
        dir = MODE_DEL;
        break;
      case '=':
        dir = MODE_QUERY;
        break;
      default:
      {
        const struct chan_mode *mode = cmode_map[(unsigned char)*ml];

        if (mode)
          mode->func(source_p, chptr, parc, &parn, parv, &errors, alevel, dir, *ml, mode);
        else
          chm_nosuch(source_p, chptr, parc, &parn, parv, &errors, alevel, dir, *ml, NULL);
        break;
      }
    }
  }

  /* Bail out if we have nothing to do... */
  if (!mode_count)
    return;

  send_mode_changes_client(source_p, chptr);
  send_mode_changes_server(source_p, chptr);
}
