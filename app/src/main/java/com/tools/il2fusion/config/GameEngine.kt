package com.tools.il2fusion.config

import androidx.annotation.StringRes
import com.tools.il2fusion.R

enum class GameEngine(
    val storageValue: String,
    val nativeValue: Int,
    @StringRes val displayNameRes: Int
) {
    UnityIl2Cpp("unity_il2cpp", 0, R.string.engine_unity_il2cpp),
    Cocos2dxLua("cocos2dx_lua", 1, R.string.engine_cocos2dx_lua);

    companion object {
        fun fromStorageValue(value: String?): GameEngine {
            return entries.firstOrNull { it.storageValue == value } ?: UnityIl2Cpp
        }
    }
}
