// license:BSD-3-Clause
// copyright-holders:David Sexton
package org.doubledroid.tts

/**
 * Emoji-to-text translation, run before [DoubleTalkEngine.sanitizeText]'s
 * ASCII strip so a recognised emoji speaks as its name (e.g. "grinning face")
 * instead of vanishing into a word-breaking space. Emoji are outside the BMP
 * (and flags/skin tones/ZWJ sequences span multiple code points), so this
 * walks Unicode code points rather than UTF-16 chars like the rest of
 * [DoubleTalkEngine]'s text handling does.
 */
internal object Emoji {

    private const val VARIATION_SELECTOR_15 = 0xFE0E
    private const val VARIATION_SELECTOR_16 = 0xFE0F
    private const val ZERO_WIDTH_JOINER = 0x200D
    private const val REGIONAL_INDICATOR_BASE = 0x1F1E6 // 'A'
    private val REGIONAL_INDICATORS = REGIONAL_INDICATOR_BASE..0x1F1FF // 'A'-'Z'

    /**
     * Replaces each recognised emoji code point with " <description> "
     * (space-padded so it word-breaks from surrounding text). A pair of
     * regional-indicator letters (a flag) is translated as a unit. A
     * skin-tone modifier always immediately follows the emoji it colors, so
     * (when that emoji is one we described) it's appended as an extra word
     * rather than spoken on its own - "raising hands" + light skin tone
     * modifier becomes "raising hands light skin tone". The emoji variation
     * selector and zero-width joiners are dropped silently - a ZWJ family
     * sequence falls back to speaking each member emoji in turn. Anything
     * unrecognised passes through unchanged for
     * [DoubleTalkEngine.sanitizeText] to handle.
     */
    fun describe(text: String): String {
        val codePoints = text.codePoints().toArray()
        val out = StringBuilder(text.length)
        var i = 0
        while (i < codePoints.size) {
            val cp = codePoints[i]
            val name = NAMES[cp]
            val skinTone = SKIN_TONES[cp]
            when {
                name != null -> out.append(' ').append(name).append(' ')
                cp in REGIONAL_INDICATORS && i + 1 < codePoints.size &&
                    codePoints[i + 1] in REGIONAL_INDICATORS -> {
                    val code = "" + regionalLetter(cp) + regionalLetter(codePoints[i + 1])
                    out.append(' ').append(FLAGS[code] ?: "$code flag").append(' ')
                    i++
                }
                skinTone != null -> {
                    if (i > 0 && codePoints[i - 1] in NAMES) out.append(skinTone).append(' ')
                }
                cp == VARIATION_SELECTOR_15 || cp == VARIATION_SELECTOR_16 ||
                    cp == ZERO_WIDTH_JOINER -> {}
                else -> out.appendCodePoint(cp)
            }
            i++
        }
        return out.toString()
    }

    private fun regionalLetter(cp: Int): Char = 'A' + (cp - REGIONAL_INDICATOR_BASE)

    /** Fitzpatrick emoji modifiers (types 1-2 through 6), keyed by code point. */
    private val SKIN_TONES = mapOf(
        0x1F3FB to "light skin tone",
        0x1F3FC to "medium-light skin tone",
        0x1F3FD to "medium skin tone",
        0x1F3FE to "medium-dark skin tone",
        0x1F3FF to "dark skin tone",
    )

    private val FLAGS = mapOf(
        "US" to "United States flag", "GB" to "United Kingdom flag",
        "CA" to "Canada flag", "AU" to "Australia flag", "NZ" to "New Zealand flag",
        "IE" to "Ireland flag", "FR" to "France flag", "DE" to "Germany flag",
        "ES" to "Spain flag", "IT" to "Italy flag", "PT" to "Portugal flag",
        "NL" to "Netherlands flag", "SE" to "Sweden flag", "CH" to "Switzerland flag",
        "JP" to "Japan flag", "CN" to "China flag", "IN" to "India flag",
        "KR" to "South Korea flag", "BR" to "Brazil flag", "MX" to "Mexico flag",
        "RU" to "Russia flag", "ZA" to "South Africa flag", "EG" to "Egypt flag",
        "SA" to "Saudi Arabia flag", "AE" to "United Arab Emirates flag",
        "EU" to "European Union flag",
    )

    // Codepoint -> spoken description. Verified against Unicode character
    // data before inclusion; descriptions are simplified from the official
    // names for natural TTS phrasing.
    private val NAMES: Map<Int, String> = mapOf(
        0x231A to "watch", // ⌚
        0x231B to "hourglass", // ⌛
        0x23F0 to "alarm clock", // ⏰
        0x2600 to "sun", // ☀
        0x2601 to "cloud", // ☁
        0x2602 to "umbrella", // ☂
        0x260E to "telephone", // ☎
        0x2615 to "hot beverage", // ☕
        0x2620 to "skull and crossbones", // ☠
        0x262E to "peace symbol", // ☮
        0x267B to "recycling symbol", // ♻
        0x26A0 to "warning sign", // ⚠
        0x26A1 to "high voltage", // ⚡
        0x26BD to "soccer ball", // ⚽
        0x26D4 to "no entry", // ⛔
        0x2705 to "check mark", // ✅
        0x2708 to "airplane", // ✈
        0x2709 to "envelope", // ✉
        0x270A to "raised fist", // ✊
        0x270B to "raised hand", // ✋
        0x270C to "victory hand", // ✌
        0x270D to "writing hand", // ✍
        0x270F to "pencil", // ✏
        0x2728 to "sparkles", // ✨
        0x2744 to "snowflake", // ❄
        0x274C to "cross mark", // ❌
        0x2753 to "question mark", // ❓
        0x2757 to "exclamation mark", // ❗
        0x2764 to "red heart", // ❤
        0x2B50 to "star", // ⭐
        0x1F308 to "rainbow", // 🌈
        0x1F30D to "globe showing Europe and Africa", // 🌍
        0x1F30E to "globe showing Americas", // 🌎
        0x1F319 to "crescent moon", // 🌙
        0x1F31E to "sun with face", // 🌞
        0x1F31F to "glowing star", // 🌟
        0x1F327 to "cloud with rain", // 🌧
        0x1F32D to "hot dog", // 🌭
        0x1F32E to "taco", // 🌮
        0x1F347 to "grapes", // 🍇
        0x1F349 to "watermelon", // 🍉
        0x1F34B to "lemon", // 🍋
        0x1F34C to "banana", // 🍌
        0x1F34D to "pineapple", // 🍍
        0x1F34E to "red apple", // 🍎
        0x1F353 to "strawberry", // 🍓
        0x1F354 to "hamburger", // 🍔
        0x1F355 to "pizza", // 🍕
        0x1F35F to "french fries", // 🍟
        0x1F363 to "sushi", // 🍣
        0x1F368 to "ice cream", // 🍨
        0x1F369 to "doughnut", // 🍩
        0x1F36A to "cookie", // 🍪
        0x1F36B to "chocolate bar", // 🍫
        0x1F36C to "candy", // 🍬
        0x1F375 to "teacup", // 🍵
        0x1F377 to "wine glass", // 🍷
        0x1F378 to "cocktail glass", // 🍸
        0x1F37A to "beer mug", // 🍺
        0x1F37F to "popcorn", // 🍿
        0x1F381 to "wrapped gift", // 🎁
        0x1F382 to "birthday cake", // 🎂
        0x1F388 to "balloon", // 🎈
        0x1F389 to "party popper", // 🎉
        0x1F3AE to "video game", // 🎮
        0x1F3B5 to "musical note", // 🎵
        0x1F3B8 to "guitar", // 🎸
        0x1F3BE to "tennis", // 🎾
        0x1F3C0 to "basketball", // 🏀
        0x1F3C5 to "medal", // 🏅
        0x1F3C6 to "trophy", // 🏆
        0x1F3C8 to "American football", // 🏈
        0x1F3E0 to "house", // 🏠
        0x1F3E5 to "hospital", // 🏥
        0x1F40C to "snail", // 🐌
        0x1F40D to "snake", // 🐍
        0x1F412 to "monkey", // 🐒
        0x1F414 to "chicken", // 🐔
        0x1F418 to "elephant", // 🐘
        0x1F419 to "octopus", // 🐙
        0x1F41D to "honeybee", // 🐝
        0x1F41F to "fish", // 🐟
        0x1F422 to "turtle", // 🐢
        0x1F426 to "bird", // 🐦
        0x1F427 to "penguin", // 🐧
        0x1F428 to "koala", // 🐨
        0x1F42C to "dolphin", // 🐬
        0x1F42D to "mouse face", // 🐭
        0x1F42E to "cow face", // 🐮
        0x1F42F to "tiger face", // 🐯
        0x1F430 to "rabbit face", // 🐰
        0x1F431 to "cat face", // 🐱
        0x1F433 to "whale", // 🐳
        0x1F435 to "monkey face", // 🐵
        0x1F436 to "dog face", // 🐶
        0x1F437 to "pig face", // 🐷
        0x1F438 to "frog face", // 🐸
        0x1F439 to "hamster face", // 🐹
        0x1F43B to "bear face", // 🐻
        0x1F43C to "panda face", // 🐼
        0x1F440 to "eyes", // 👀
        0x1F441 to "eye", // 👁
        0x1F446 to "backhand index pointing up", // 👆
        0x1F447 to "backhand index pointing down", // 👇
        0x1F448 to "backhand index pointing left", // 👈
        0x1F449 to "backhand index pointing right", // 👉
        0x1F44A to "oncoming fist", // 👊
        0x1F44B to "waving hand", // 👋
        0x1F44C to "OK hand", // 👌
        0x1F44D to "thumbs up", // 👍
        0x1F44E to "thumbs down", // 👎
        0x1F44F to "clapping hands", // 👏
        0x1F450 to "open hands", // 👐
        0x1F47B to "ghost", // 👻
        0x1F47D to "alien", // 👽
        0x1F47E to "alien monster", // 👾
        0x1F480 to "skull", // 💀
        0x1F485 to "nail polish", // 💅
        0x1F493 to "beating heart", // 💓
        0x1F494 to "broken heart", // 💔
        0x1F495 to "two hearts", // 💕
        0x1F496 to "sparkling heart", // 💖
        0x1F497 to "growing heart", // 💗
        0x1F498 to "heart with arrow", // 💘
        0x1F499 to "blue heart", // 💙
        0x1F49A to "green heart", // 💚
        0x1F49B to "yellow heart", // 💛
        0x1F49C to "purple heart", // 💜
        0x1F49E to "revolving hearts", // 💞
        0x1F4A3 to "bomb", // 💣
        0x1F4A9 to "pile of poo", // 💩
        0x1F4AA to "flexed biceps", // 💪
        0x1F4AF to "hundred points", // 💯
        0x1F4B0 to "money bag", // 💰
        0x1F4B3 to "credit card", // 💳
        0x1F4BB to "computer", // 💻
        0x1F4D6 to "book", // 📖
        0x1F4F7 to "camera", // 📷
        0x1F511 to "key", // 🔑
        0x1F512 to "locked padlock", // 🔒
        0x1F525 to "fire", // 🔥
        0x1F527 to "wrench", // 🔧
        0x1F528 to "hammer", // 🔨
        0x1F577 to "spider", // 🕷
        0x1F590 to "hand with fingers splayed", // 🖐
        0x1F595 to "middle finger", // 🖕
        0x1F596 to "vulcan salute", // 🖖
        0x1F5A4 to "black heart", // 🖤
        0x1F600 to "grinning face", // 😀
        0x1F601 to "grinning face with smiling eyes", // 😁
        0x1F602 to "face with tears of joy", // 😂
        0x1F603 to "smiling face with open mouth", // 😃
        0x1F604 to "smiling face with smiling eyes", // 😄
        0x1F605 to "smiling face with cold sweat", // 😅
        0x1F606 to "laughing face", // 😆
        0x1F607 to "smiling face with halo", // 😇
        0x1F609 to "winking face", // 😉
        0x1F60A to "smiling face", // 😊
        0x1F60B to "face savoring food", // 😋
        0x1F60C to "relieved face", // 😌
        0x1F60D to "heart eyes face", // 😍
        0x1F60E to "smiling face with sunglasses", // 😎
        0x1F60F to "smirking face", // 😏
        0x1F610 to "neutral face", // 😐
        0x1F611 to "expressionless face", // 😑
        0x1F612 to "unamused face", // 😒
        0x1F613 to "face with cold sweat", // 😓
        0x1F614 to "pensive face", // 😔
        0x1F615 to "confused face", // 😕
        0x1F616 to "confounded face", // 😖
        0x1F617 to "kissing face", // 😗
        0x1F618 to "face blowing a kiss", // 😘
        0x1F619 to "kissing face with smiling eyes", // 😙
        0x1F61A to "kissing face with closed eyes", // 😚
        0x1F61B to "face with tongue sticking out", // 😛
        0x1F61C to "winking face with tongue", // 😜
        0x1F61D to "squinting face with tongue", // 😝
        0x1F61E to "disappointed face", // 😞
        0x1F61F to "worried face", // 😟
        0x1F620 to "angry face", // 😠
        0x1F621 to "pouting face", // 😡
        0x1F622 to "crying face", // 😢
        0x1F623 to "persevering face", // 😣
        0x1F624 to "triumphant face", // 😤
        0x1F625 to "disappointed but relieved face", // 😥
        0x1F626 to "frowning face with open mouth", // 😦
        0x1F627 to "anguished face", // 😧
        0x1F628 to "fearful face", // 😨
        0x1F629 to "weary face", // 😩
        0x1F62A to "sleepy face", // 😪
        0x1F62B to "tired face", // 😫
        0x1F62C to "grimacing face", // 😬
        0x1F62D to "loudly crying face", // 😭
        0x1F62E to "face with open mouth", // 😮
        0x1F62F to "hushed face", // 😯
        0x1F630 to "anxious face with sweat", // 😰
        0x1F631 to "face screaming in fear", // 😱
        0x1F632 to "astonished face", // 😲
        0x1F633 to "flushed face", // 😳
        0x1F634 to "sleeping face", // 😴
        0x1F635 to "dizzy face", // 😵
        0x1F636 to "face without mouth", // 😶
        0x1F637 to "face with medical mask", // 😷
        0x1F641 to "slightly frowning face", // 🙁
        0x1F642 to "slightly smiling face", // 🙂
        0x1F643 to "upside down face", // 🙃
        0x1F644 to "face with rolling eyes", // 🙄
        0x1F64C to "raising hands", // 🙌
        0x1F64F to "folded hands", // 🙏
        0x1F680 to "rocket", // 🚀
        0x1F682 to "steam locomotive", // 🚂
        0x1F68C to "bus", // 🚌
        0x1F697 to "car", // 🚗
        0x1F90C to "pinched fingers", // 🤌
        0x1F90D to "white heart", // 🤍
        0x1F90E to "brown heart", // 🤎
        0x1F90F to "pinching hand", // 🤏
        0x1F910 to "zipper mouth face", // 🤐
        0x1F911 to "money mouth face", // 🤑
        0x1F912 to "face with thermometer", // 🤒
        0x1F913 to "nerd face", // 🤓
        0x1F914 to "thinking face", // 🤔
        0x1F915 to "face with head bandage", // 🤕
        0x1F916 to "robot", // 🤖
        0x1F917 to "hugging face", // 🤗
        0x1F918 to "sign of the horns", // 🤘
        0x1F919 to "call me hand", // 🤙
        0x1F91A to "raised back of hand", // 🤚
        0x1F91D to "handshake", // 🤝
        0x1F91E to "crossed fingers", // 🤞
        0x1F91F to "love you gesture", // 🤟
        0x1F920 to "cowboy hat face", // 🤠
        0x1F921 to "clown face", // 🤡
        0x1F922 to "nauseated face", // 🤢
        0x1F923 to "rolling on the floor laughing", // 🤣
        0x1F924 to "drooling face", // 🤤
        0x1F925 to "lying face", // 🤥
        0x1F926 to "face palm", // 🤦
        0x1F927 to "sneezing face", // 🤧
        0x1F928 to "face with raised eyebrow", // 🤨
        0x1F929 to "star struck face", // 🤩
        0x1F92A to "zany face", // 🤪
        0x1F92B to "shushing face", // 🤫
        0x1F92C to "face with symbols on mouth", // 🤬
        0x1F92D to "face with hand over mouth", // 🤭
        0x1F92E to "face vomiting", // 🤮
        0x1F92F to "exploding head", // 🤯
        0x1F933 to "selfie", // 🤳
        0x1F950 to "croissant", // 🥐
        0x1F951 to "avocado", // 🥑
        0x1F955 to "carrot", // 🥕
        0x1F95A to "egg", // 🥚
        0x1F966 to "broccoli", // 🥦
        0x1F96A to "sandwich", // 🥪
        0x1F970 to "smiling face with hearts", // 🥰
        0x1F971 to "yawning face", // 🥱
        0x1F972 to "smiling face with tear", // 🥲
        0x1F973 to "partying face", // 🥳
        0x1F975 to "hot face", // 🥵
        0x1F976 to "cold face", // 🥶
        0x1F978 to "disguised face", // 🥸
        0x1F979 to "face holding back tears", // 🥹
        0x1F97A to "pleading face", // 🥺
        0x1F981 to "lion face", // 🦁
        0x1F984 to "unicorn face", // 🦄
        0x1F985 to "eagle", // 🦅
        0x1F989 to "owl", // 🦉
        0x1F98A to "fox face", // 🦊
        0x1F98B to "butterfly", // 🦋
        0x1F98C to "deer", // 🦌
        0x1F994 to "hedgehog", // 🦔
        0x1F9D0 to "face with monocle", // 🧐
        0x1F9E1 to "orange heart", // 🧡
    )
}
