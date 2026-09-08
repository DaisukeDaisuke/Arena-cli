#include "game.h"

namespace arena {
    EffectFn conditional_add_handler() {
        return +[](EffectContext &c, const Effect &e) {
            if ((e.arg0 == 0 && c.opponent.guard) || (e.arg0 == 1 && c.opponent.burn > 0) ||
                (e.arg0 == 2 && c.opponent.pending.power > 0))
                c.add += e.arg1;
        };
    }

    EffectFn drain_handler() {
        return +[](EffectContext &c, const Effect &) {
            c.user.hp = std::min(c.user.max_hp, c.user.hp + c.damage / 2);
        };
    }

    EffectFn spend_bank_handler() {
        return +[](EffectContext &c, const Effect &) {
            c.add += c.user.bank;
            c.user.bank = 0;
        };
    }

    EffectFn schedule_handler() {
        return +[](EffectContext &c, const Effect &e) {
            c.user.pending = {
                c.skill, (e.arg0 < 0 ? c.captured.power : e.arg0) + c.add, e.arg0 < 0 ? c.captured.magic : e.arg1 != 0
            };
            event(c.run,
                  std::string(c.player ? "YOU" : "ENEMY") + " CHARGE / POWER " + std::to_string(c.user.pending.power));
            if (e.arg0 < 0) event(c.run, std::string("BORROWED ") + skills[c.captured.skill].name);
        };
    }

    EffectFn borrow_handler() { return +[](EffectContext &c, const Effect &) { c.captured = c.opponent.pending; }; }
}
