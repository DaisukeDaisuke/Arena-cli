#include "game.h"

namespace arena {
    namespace {
        Effect fx(Opcode op, int a = 0, int b = 0, Phase p = Phase::After, Target t = Target::Self) {
            return {op, t, a, b, p};
        }

        Effect hit(int power, bool magic = false, bool pierce = false) {
            return fx(Opcode::Damage, power, (magic ? 1 : 0) | (pierce ? 2 : 0), Phase::Hit, Target::Opponent);
        }

        SkillDef skill(const char *n, const char *j, const char *d, int mp, std::initializer_list<Effect> e,
                       InputPolicy p = InputPolicy::GlobalName) { return {n, j, d, mp, p, e}; }
    }

    const std::array<SkillDef, 20> skills = {
        {
            skill("attack", "通常攻撃", "物理12", 0, {hit(12)}),
            skill("guard", "防御", "直接攻撃を半減、MP+4。軽減分を最大12蓄積", 0, {
                      fx(Opcode::Apply, 2, 0, Phase::Stance), fx(Opcode::RestoreMP, 4)
                  }),
            skill("wait", "待機", "防御せずMP+6", 0, {fx(Opcode::RestoreMP, 6)}),
            skill("pierce", "貫き", "物理14、Guard無視", 2, {hit(14, false, true)}),
            skill("smash", "強打", "物理22、相手Guard時は基礎-6", 3, {fx(Opcode::ConditionalAdd, 0, -6, Phase::Prepare), hit(22)}),
            skill("fire", "火炎", "魔法12、Burn: この回から2回、各3ダメージ", 3, {
                      hit(12, true), fx(Opcode::Apply, 1, 0, Phase::After, Target::Opponent)
                  }),
            skill("flare", "追い炎", "魔法12、相手Burn中は+14", 4, {
                      fx(Opcode::ConditionalAdd, 1, 14, Phase::Prepare), hit(12, true)
                  }),
            skill("drain", "吸収", "魔法14、実ダメージの半分を回復", 4, {hit(14, true), fx(Opcode::Drain)}),
            skill("healing-rain", "癒しの雨", "自分HP12、相手HP6回復。繰り返し使用可能", 4, {
                      fx(Opcode::Heal, 12), fx(Opcode::Heal, 6, 0, Phase::After, Target::Opponent)
                  }),
            skill("focus", "集中", "次の直接攻撃を1.5倍。溜め開始では消費しない", 4, {fx(Opcode::Apply, 0)}),
            skill("barrier", "魔法障壁", "この回の魔法直接ダメージを0にする", 3, {fx(Opcode::Apply, 3, 0, Phase::Stance)}),
            skill("purge", "浄化", "魔法10、相手のFocusとBurnを解除", 3, {
                      hit(10, true), fx(Opcode::Clear, 3, 0, Phase::After, Target::Opponent)
                  }),
            skill("counter", "迎撃", "物理10、実行時に相手が発動前の予約を持つなら+18", 3, {
                      fx(Opcode::ConditionalAdd, 2, 18, Phase::Prepare), hit(10)
                  }),
            skill("siphon", "魔力吸収", "魔法8、自分MP+5。相手MPは減らさない", 2, {hit(8, true), fx(Opcode::RestoreMP, 5)}),
            skill("mend", "手当て", "自分HP12回復、Burn解除", 3, {fx(Opcode::Heal, 12), fx(Opcode::Clear, 2)}),
            skill("bank-shot", "蓄勢撃", "物理14+Bank、Bankを消費", 3, {fx(Opcode::SpendBank, 0, 0, Phase::Prepare), hit(14)}),
            skill("quake", "地鳴り", "物理36。溜め→発動→休息", 6, {fx(Opcode::Schedule, 36, 0, Phase::Prepare)},
                  InputPolicy::EquippedOnly),
            skill("eclipse", "蝕", "魔法48。溜め→発動→休息", 8, {fx(Opcode::Schedule, 48, 1, Phase::Prepare)},
                  InputPolicy::EquippedOnly),
            skill("feint", "牽制", "物理8、攻撃後に自分Focus付与", 2, {hit(8), fx(Opcode::Apply, 0)}),
            skill("reprise", "借り技", "入力時の相手の発動前予約を捕捉しBankを加える。自分の溜め→発動→休息。Focusは発動時に適用", 10, {
                      fx(Opcode::Borrow, 0, 0, Phase::Capture), fx(Opcode::SpendBank, 0, 0, Phase::Prepare),
                      fx(Opcode::Schedule, -1, 0, Phase::Prepare)
                  })
        }
    };
    const std::array<EnemyDef, 10> enemies = {
        {
            {"IRON HOUND", 36, 110, 1, {{{3}, {4, 18}, {9, 12}}}},
            {"HEX MAGE", 34, 80, 2, {{{5}, {6, 11}, {8, 13}}}},
            {"STONE GOLEM", 48, 40, 3, {{{4, 15}, {16}, {9, 8}}}},
            {"ASH WISP", 36, 120, 2, {{{5}, {6, 18}, {8, 13}}}},
            {"SHADE", 40, 130, 2, {{{18, 3}, {11, 4}, {12, 13}}}},
            {"IRON MONK", 44, 90, 0, {{{3}, {9, 18}, {12, 8}}}},
            {"BONE ARCHER", 42, 120, 4, {{{3}, {18, 4}, {13, 12}}}},
            {"FROST WITCH", 42, 80, 2, {{{11}, {5, 3}, {8}}}},
            {"BLADE DANCER", 54, 140, 4, {{{18}, {12, 3}, {4, 5}}}},
            {"VOID KNIGHT", 84, 90, 0, {{{17}, {11, 3}, {13, 8}}}}
        }
    };
    const std::array<ItemDef, 10> items = {
        {
            {"iron-coat", "鉄の外套", "体力を厚くする。少し遅くなる", 80, false, false, {{{1, 1, 32}, {1, 1, 0}, {1, 1, -20}}}},
            {"lead-plate", "鉛の鎧", "体力を大きく増やす。素早さ0", 140, false, false, {{{1, 1, 100}, {1, 1, 0}, {0, 1, 0}}}},
            {"pilgrim-robe", "巡礼の衣", "体力と魔力を伸ばす。速さはそのまま", 100, false, false, {{{1, 1, 12}, {1, 1, 16}, {1, 1, 0}}}},
            {"wing-boots", "翼の靴", "防具込みの素早さを2倍。0は増えない", 100, true, false, {{{1, 1, 0}, {1, 1, 0}, {2, 1, 0}}}},
            {"mana-ring", "魔力の指輪", "他の能力を落とさず魔力+12", 60, true, false, {{{1, 1, 0}, {1, 1, 12}, {1, 1, 0}}}},
            {"quick-pin", "先駆けの留め金", "防具込みの素早さ+100", 100, true, true, {{{1, 1, 0}, {1, 1, 0}, {1, 1, 100}}}},
            {"duelist-jacket", "決闘士の上着", "体力を削り素早さ+50", 90, false, false, {{{1, 1, -16}, {1, 1, 0}, {1, 1, 50}}}},
            {"reservoir-robe", "貯魔の法衣", "体力を削り基礎魔力2倍", 120, false, false, {{{1, 1, -24}, {2, 1, 0}, {1, 1, 0}}}},
            {"heart-knot", "命結びのお守り", "魔力を4減らし体力+24", 60, true, false, {{{1, 1, 24}, {1, 1, -4}, {1, 1, 0}}}},
            {"glass-prism", "硝子の魔晶", "防具込みの体力3/4、魔力+24、素早さ+20", 100, true, false, {{{3, 4, 0}, {1, 1, 24}, {1, 1, 20}}}}
        }
    };
    const std::array<std::array<const char *, 16>, 6> art = {
        {
            {
                {
                    "...l........l...", "..lbl......lbl..", "..lbbllllllbbl..", "...lbbbbbbbbl...", "...lbeebbeebl...",
                    "...lbbssssbbl...", "....lbllllbl....", "..lllbbaabblll..", ".lbbblbaablbbbl.", ".lb..lbbbbl..bl.",
                    ".l...lbbbbl....l", ".....lbbbbl.....", "....llbllbll....", "....lbbl.lbbl...", "...lbbbl.lbbbl..",
                    "...lllll.lllll.."
                }
            },
            {
                {
                    "................", "..l........l....", "..ll......ll....", "..lbl....lbl....", "..lbbllllbbl....",
                    "...lbeebebl.....", "...lbbbbbbllll..", "....lssbbbbbbbl.", ".....llbbbbbbbl.", ".....lbbbbbbbl..",
                    "....lbbllllbbl..", "....lbl...lbl...", "....lbl...lbl...", "...lbbl..lbbl...", "...lll...lll....",
                    "................"
                }
            },
            {
                {
                    ".......l........", "......lbl.......", ".....lbbbl......", "....lbbbbbl.....", "...lllllllll....",
                    ".....seees......", ".....sbsbs......", "......sss.......", "....llaaall.....", "...lbbbabbb.l...",
                    "..lbbbbabbbbl...", "..lbblbabblbl...", "....lbbabbl.....", "...lbbbabbb.l...", "..lbbbbabbbbl...",
                    "..lllllllllll..."
                }
            },
            {
                {
                    ".....llllll.....", "....lbbbb.b.l...", "....lbeebebl....", "....lbbssbbl....", ".....llllll.....",
                    "..llllaaaallll..", ".lbbbbbaabbbbbl.", ".lbbbbbaabbbbbl.", ".lbbllbaabllbbl.", ".lbbl.lbb.l.lbbl",
                    ".llll.lbb.l.llll", "......lbbl......", "....llbllbll....", "...lbbb..bbbl...", "...lbbb..bbbl...",
                    "...llll..llll..."
                }
            },
            {
                {
                    ".............l..", ".....llll...ll..", "....lbbbbl..l...", "....lbeebl.l....", ".....lss.l.l....",
                    "......ll..l.....", "....llaall......", "...lbbbabbl.....", "..lbbblab.bll...", ".lll..lbbl..ll..",
                    "......lbbl......", ".....lbllbl.....", "....lbbl.lbl....", "...lbbl..lbbl...", "...lll....lbbl..",
                    "..........llll.."
                }
            },
            {
                {
                    ".......ee.......", "...e...aa...e...", "...aa..aa..aa...", "...abaabaabaa...", "....abbbbbba....",
                    "....alllllla....", "..aaabbbbbbaaa..", ".abbabbbbbbabba.", ".ab.abbbbbba.ba.", "..aa.abbbba.aa..",
                    "......abba......", "......abba......", ".....aabbaa.....", "....abbbbbba....", "...abbbbbbbba...",
                    "...aaaaaaaaaa..."
                }
            }
        }
    };
    // Character index: enemy IDs 0..9, player 10, gold 11, silver 12.
    const std::array<std::array<unsigned, 6>, 13> palettes = {
        {
            {{0x0B1020, 0x202A36, 0x687A8C, 0xC8D6E5, 0xFF637D, 0xD89050}},
            {{0x0B1020, 0x281D3C, 0x7952A8, 0xD9B9FF, 0x8DFFDA, 0xEF78C9}},
            {{0x0B1020, 0x2B3038, 0x6B747D, 0xC9D0D4, 0xFFC857, 0xA29272}},
            {{0x0B1020, 0x39231E, 0xBA4F34, 0xFFC28A, 0xFFFFFF, 0xFF8B38}},
            {{0x0B1020, 0x161D30, 0x35405D, 0x899ABF, 0xFF637D, 0x7067BA}},
            {{0x0B1020, 0x382B20, 0x916A3C, 0xEBD9A3, 0xFFFFFF, 0xF3AC45}},
            {{0x0B1020, 0x383529, 0xA69C7E, 0xEBE2C8, 0xFF637D, 0x8DCAA7}},
            {{0x0B1020, 0x183A4A, 0x398FAB, 0xB4EEFF, 0xFFFFFF, 0x73D8FA}},
            {{0x0B1020, 0x362033, 0x964C7E, 0xF4C8E3, 0xFFFFFF, 0xFF7696}},
            {{0x0B1020, 0x211A38, 0x514174, 0xC2B0F2, 0xFF637D, 0xB386FF}},
            {{0x0B1020, 0x1E3548, 0x287F91, 0xB8F4FF, 0xFFFFFF, 0x66FFD1}},
            {{0x0B1020, 0x5C3418, 0xD69B23, 0xFFE7A0, 0xFFFFF0, 0xFFCD55}},
            {{0x0B1020, 0x293440, 0x8795A4, 0xDBE5EF, 0xFFFFFF, 0xB5C2CF}}
        }
    };

    bool validate_data(std::string &error) {
        for (const auto &s: skills) {
            if (s.effects.empty() || s.effects.size() > 3 || s.mp < 0) {
                error = "Invalid skill";
                return false;
            }
            for (const auto &e: s.effects)
                if (static_cast<unsigned>(e.op) >= 10) {
                    error = "Invalid opcode";
                    return false;
                }
        }
        for (const auto &e: enemies)
            for (const auto &slot: e.candidates) {
                if (slot.empty() || slot.size() > 10) {
                    error = "Invalid AI slot";
                    return false;
                }
                for (int id: slot)
                    if (id < 0 || id >= 20) {
                        error = "Invalid skill reference";
                        return false;
                    }
            }
        for (const auto &sprite: art)
            for (const auto *line: sprite) {
                if (std::string(line).size() != 16 || std::string(line).find_first_not_of(".sblea") !=
                    std::string::npos) {
                    error = "Invalid 16x16 art";
                    return false;
                }
            }
        for (const auto &i: items) {
            if (i.price < 0) return false;
            for (auto t: i.stats) if (t.mul < 0 || t.div < 1) return false;
        }
        return true;
    }
}
