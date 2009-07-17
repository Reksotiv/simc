// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simcraft.h"

// ==========================================================================
// Attack
// ==========================================================================

// attack_t::attack_t =======================================================

attack_t::attack_t( const char* n, player_t* p, int resource, int school, int tree, bool special ) :
    action_t( ACTION_ATTACK, n, p, resource, school, tree, special ),
    base_expertise( 0 ), player_expertise( 0 ), target_expertise( 0 )
{
  may_miss = may_resist = may_dodge = may_parry = may_glance = may_block = true;

  if ( special ) may_glance = false;

  if ( player -> position == POSITION_BACK )
  {
    may_block = false;
    may_parry = false;
  }
  else if ( player -> position == POSITION_RANGED )
  {
    may_block  = false;
    may_dodge  = false;
    may_glance = false; // FIXME! If we decide to make ranged auto-shot become "special" this line goes away.
    may_parry  = false;
  }

  base_attack_power_multiplier = 1.0;
  base_crit_bonus = 1.0;

  trigger_gcd = p -> base_gcd;
  min_gcd = 1.0;

  range = 0; // Prevent action from being scheduled when player_t::moving!=0
}

// attack_t::haste ==========================================================

double attack_t::haste() SC_CONST
{
  player_t* p = player;
  double h = p -> attack_haste;

  if ( p -> type != PLAYER_GUARDIAN )
  {
    if ( p -> buffs.bloodlust )
    {
      h *= 1.0 / ( 1.0 + 0.30 );
    }

    if ( sim -> auras.swift_retribution || sim -> auras.improved_moonkin )
    {
      h *= 1.0 / ( 1.0 + 0.03 );
    }

    if ( p -> position != POSITION_RANGED && p -> buffs.windfury_totem != 0 )
    {
      h *= 1.0 / ( 1.0 + p -> buffs.windfury_totem );
    }

    if( sim -> auras.celerity )
    {
      h *= 1.0 / ( 1.0 + 0.20 );
    }

    if ( p -> buffs.mongoose_mh ) h *= 1.0 / ( 1.0 + 0.02 );
    if ( p -> buffs.mongoose_oh ) h *= 1.0 / ( 1.0 + 0.02 );
  }

  return h;
}

// attack_t::execute_time ===================================================

double attack_t::execute_time() SC_CONST
{
  if ( base_execute_time == 0 ) return 0;
  return base_execute_time * haste();
}

// attack_t::player_buff ====================================================

void attack_t::player_buff()
{
  action_t::player_buff();

  player_t* p = player;

  player_hit       = p -> composite_attack_hit();
  player_expertise = p -> composite_attack_expertise();
  player_crit      = p -> composite_attack_crit();

  if ( p -> meta_gem == META_CHAOTIC_SKYFLARE ||
       p -> meta_gem == META_RELENTLESS_EARTHSIEGE )
  {
    player_crit_multiplier *= 1.03;
  }

  if ( sim -> debug )
    log_t::output( sim, "attack_t::player_buff: %s hit=%.2f expertise=%.2f crit=%.2f crit_multiplier=%.2f",
                   name(), player_hit, player_expertise, player_crit, player_crit_multiplier );
}

// attack_t::target_debuff ==================================================

void attack_t::target_debuff( int dmg_type )
{
  action_t::target_debuff( dmg_type );

  target_expertise = 0;
}

// attack_t::miss_chance ====================================================

double attack_t::miss_chance( int delta_level ) SC_CONST
{
  if ( delta_level > 2 )
  {
    // FIXME: needs testing for delta_level > 3
    return 0.07 + ( delta_level - 2 ) * 0.01 - total_hit();
  }
  else
  {
    return 0.05 + delta_level * 0.005 - total_hit();
  }
}

// attack_t::dodge_chance ===================================================

double attack_t::dodge_chance( int delta_level ) SC_CONST
{
  return 0.05 + delta_level * 0.005 - total_expertise();
}

// attack_t::parry_chance ===================================================

double attack_t::parry_chance( int delta_level ) SC_CONST
{
  return 0.05 + delta_level * 0.005 - total_expertise();
}

// attack_t::glance_chance ==================================================

double attack_t::glance_chance( int delta_level ) SC_CONST
{
  return ( delta_level + 1 ) * 0.06;
}

// attack_t::block_chance ===================================================

double attack_t::block_chance( int delta_level ) SC_CONST
{
  return 0.05 + delta_level * 0.005;
}

// attack_t::crit_chance ====================================================

double attack_t::crit_chance( int delta_level ) SC_CONST
{
  double chance = total_crit();

  if( ! special )
  {
    if ( delta_level > 2 )
    {
      chance -= ( 0.03 + delta_level * 0.006 );
    }
    else
    {
      chance -= ( delta_level * 0.002 );
    }
  }

  return chance;
}

// attack_t::build_table ====================================================

int attack_t::build_table( double* chances,
                           int*    results )
{
  double miss=0, dodge=0, parry=0, glance=0, block=0, crit=0;

  int delta_level = sim -> target -> level - player -> level;

  if ( may_miss   )   miss =   miss_chance( delta_level );
  if ( may_dodge  )  dodge =  dodge_chance( delta_level );
  if ( may_parry  )  parry =  parry_chance( delta_level );
  if ( may_glance ) glance = glance_chance( delta_level );

  if ( may_block && sim -> target -> shield )
  {
    block = block_chance( delta_level );
  }

  if ( may_crit && ! special ) // Specials are 2-roll calculations
  {
    crit = crit_chance( delta_level );
  }

  if ( sim -> debug ) log_t::output( sim, "attack_t::build_table: %s miss=%.3f dodge=%.3f parry=%.3f glance=%.3f block=%.3f crit=%.3f",
                                       name(), miss, dodge, parry, glance, block, crit );

  double total = 0;
  int num_results = 0;

  if ( miss > 0 )
  {
    total += miss;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_MISS;
    num_results++;
  }
  if ( dodge > 0 )
  {
    total += dodge;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_DODGE;
    num_results++;
  }
  if ( parry > 0 )
  {
    total += parry;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_PARRY;
    num_results++;
  }
  if ( glance > 0 )
  {
    total += glance;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_GLANCE;
    num_results++;
  }
  if ( block > 0 )
  {
    total += block;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_BLOCK;
    num_results++;
  }
  if ( crit > 0 )
  {
    total += crit;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_CRIT;
    num_results++;
  }
  if ( total < 1.0 )
  {
    chances[ num_results ] = 1.0;
    results[ num_results ] = RESULT_HIT;
    num_results++;
  }

  return num_results;
}

// attack_t::caclulate_result ===============================================

void attack_t::calculate_result()
{
  direct_dmg = 0;

  double chances[ RESULT_MAX ];
  int    results[ RESULT_MAX ];

  result = RESULT_NONE;

  if ( ! harmful ) return;

  int num_results = build_table( chances, results );

  if ( num_results == 1 )
  {
    result = results[ 0 ];
  }
  else
  {
    if ( sim -> smooth_rng )
    {
      // Encode 1-roll attack table into equivalent multi-roll system

      double prev_chance = 0.0;

      for ( int i=0; i < num_results-1; i++ )
      {
        if ( rng[ results[ i ] ] -> roll( ( chances[ i ] - prev_chance ) / ( 1.0 - prev_chance ) ) )
        {
          result = results[ i ];
          break;
        }
        prev_chance = chances[ i ];
      }
      if ( result == RESULT_NONE ) result = results[ num_results-1 ];
    }
    else
    {
      // 1-roll attack table with true RNG

      double random = sim -> rng -> real();

      for ( int i=0; i < num_results; i++ )
      {
        if ( random <= chances[ i ] )
        {
          result = results[ i ];
          break;
        }
      }
    }
  }

  assert( result != RESULT_NONE );

  if ( result_is_hit() )
  {
    if ( binary && rng[ RESULT_RESIST ] -> roll( resistance() ) )
    {
      result = RESULT_RESIST;
    }
    else if ( special && may_crit ) // Specials are 2-roll calculations
    {
      int delta_level = sim -> target -> level - player -> level;

      if ( rng[ RESULT_CRIT ] -> roll( crit_chance( delta_level ) ) )
      {
        result = RESULT_CRIT;
      }
    }
  }

  if ( sim -> debug ) log_t::output( sim, "%s result for %s is %s", player -> name(), name(), util_t::result_type_string( result ) );
}

// attack_t::execute =======================================================

void attack_t::execute()
{
  action_t::execute();

  action_callback_t::trigger( player -> attack_result_callbacks[ result ], this );
}
