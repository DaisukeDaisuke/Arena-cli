#include "game.h"

namespace arena {
    EffectFn damage_handler() {
        return +[](EffectContext &c, const Effect &e) {
            auto &u = c.user;
            auto &v = c.opponent;
            int power = std::max(0, e.arg0 + c.add) * (u.focus ? 3 : 2) / 2;
            u.focus = false;
            int dealt = power;
            if ((e.arg1 & 1) && v.barrier) dealt = 0;
            else if (v.guard && !(e.arg1 & 2)) {
                dealt /= 2;
                v.bank = std::min(12, v.bank + power - dealt);
            }
            c.damage = std::min(v.hp, dealt);
            v.hp -= c.damage;
            if (c.player) c.run.maximum_damage = std::max(c.run.maximum_damage, c.damage);
            else c.run.taken += c.damage;
            event(c.run,
                  std::string(c.player ? "YOU" : "ENEMY") + " POWER " + std::to_string(power) + " / DAMAGE " +
                  std::to_string(c.damage));
        };
    }

    EffectFn heal_handler() {
        return +[](EffectContext &c, const Effect &e) {
            auto &t = e.target == Target::Self ? c.user : c.opponent;
            t.hp = std::min(t.max_hp, t.hp + e.arg0);
        };
    }

    EffectFn apply_handler() {
        return +[](EffectContext &c, const Effect &e) {
            auto &t = e.target == Target::Self ? c.user : c.opponent;
            if (e.arg0 == 0) t.focus = true;
            if (e.arg0 == 1) t.burn = 2;
            if (e.arg0 == 2) t.guard = true;
            if (e.arg0 == 3) t.barrier = true;
        };
    }

    EffectFn clear_handler() {
        return +[](EffectContext &c, const Effect &e) {
            auto &t = e.target == Target::Self ? c.user : c.opponent;
            if (e.arg0 & 1) t.focus = false;
            if (e.arg0 & 2) t.burn = 0;
        };
    }

    EffectFn restore_mp_handler() {
        return +[](EffectContext &c, const Effect &e) {
            c.user.mp = std::min(c.user.max_mp, c.user.mp + e.arg0);
        };
    }
}
