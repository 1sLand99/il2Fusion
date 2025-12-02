package com.tools.textextracttool

import android.os.Bundle
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Divider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.Switch
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.tools.textextracttool.config.HookConfigStore
import com.tools.textextracttool.ui.theme.TextExtractToolTheme
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.BufferedReader
import java.io.InputStreamReader

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            TextExtractToolTheme {
                HookConfigScreen()
            }
        }
    }
}

@Composable
fun HookConfigScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val focusManager = LocalFocusManager.current
    val rvaList = remember { mutableStateListOf<String>() }
    val maxRvaCount = 20
    val defaultRvas = listOf("0x1d236e8")
    val snackbarHostState = remember { SnackbarHostState() }
    val savedCount = remember { mutableStateOf(0) }
    var dumpModeEnabled by remember { mutableStateOf(false) }
    var isLoading by remember { mutableStateOf(false) }
    val filePickerLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        scope.launch {
            if (uri != null) {
                isLoading = true
                try {
                    val added = loadRvasFromCs(context, uri, maxRvaCount)
                    if (added.isNotEmpty()) {
                        rvaList.clear()
                        rvaList.addAll(added)
                        snackbarHostState.showSnackbar("解析到 ${added.size} 条 RVA")
                    } else {
                        snackbarHostState.showSnackbar("未在文件中找到 set_Text 的 RVA")
                    }
                } finally {
                    isLoading = false
                }
            } else {
                snackbarHostState.showSnackbar("未选择文件")
            }
        }
    }
    LaunchedEffect(Unit) {
        val saved = HookConfigStore.loadRvasForApp(context)
        dumpModeEnabled = HookConfigStore.loadDumpModeForApp(context)
        if (saved.isEmpty()) {
            rvaList.clear()
            rvaList.addAll(defaultRvas)
            // 自动保存默认值，避免 LSPosed 端读取到空列表
            scope.launch {
                persistRvas(context, rvaList, snackbarHostState, savedCount)
            }
        } else {
            rvaList.clear()
            rvaList.addAll(saved.map { formatRva(it) })
            savedCount.value = saved.size
        }
    }

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        snackbarHost = {
            SnackbarHost(hostState = snackbarHostState) { data ->
                Snackbar(snackbarData = data)
            }
        }
    ) { inner ->
        Column(
            modifier = Modifier
                .padding(inner)
                .padding(16.dp)
                .verticalScroll(rememberScrollState())
                .pointerInput(Unit) {
                    detectTapGestures(onTap = { focusManager.clearFocus() })
                },
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                horizontalArrangement = Arrangement.SpaceBetween,
                modifier = Modifier.fillMaxWidth()
            ) {
                Column {
                    Text("Il2CppDumper 启动模式")
                    Text(if (dumpModeEnabled) "已开启（仅 dump，不拦截文本）" else "已关闭（仅拦截文本）")
                }
                Switch(
                    checked = dumpModeEnabled,
                    onCheckedChange = { checked ->
                        dumpModeEnabled = checked
                        scope.launch {
                            HookConfigStore.saveDumpMode(context, checked)
                            snackbarHostState.showSnackbar(if (checked) "已切换到 Dump 模式" else "已切换到 文本拦截 模式")
                        }
                    }
                )
            }
            if (isLoading) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }
            OutlinedButton(
                onClick = { filePickerLauncher.launch(arrayOf("*/*")) },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("解析文件（打开 Download 选择）")
            }
            Text("目标 RVA 列表（十六进制或十进制）")
            Text("当前已保存：${savedCount.value} 个", modifier = Modifier.padding(bottom = 4.dp))
            Card(
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    rvaList.forEachIndexed { index, value ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            OutlinedTextField(
                                value = value,
                                onValueChange = { new -> rvaList[index] = new },
                                label = { Text("RVA #${index + 1}") },
                                modifier = Modifier.weight(1f),
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii)
                            )
                            OutlinedButton(
                                onClick = { if (rvaList.size > 1) rvaList.removeAt(index) },
                                modifier = Modifier.padding(start = 8.dp)
                            ) {
                                Text("删除")
                            }
                        }
                    }
                    OutlinedButton(
                        onClick = {
                            if (rvaList.size >= maxRvaCount) {
                                scope.launch {
                                    snackbarHostState.showSnackbar("最多支持 $maxRvaCount 条 RVA")
                                }
                                return@OutlinedButton
                            }
                            rvaList.add("")
                        }
                    ) {
                        Text("添加一行")
                    }
                }
            }
            Divider()
            Row(
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Button(
                    onClick = {
                        scope.launch { persistRvas(context, rvaList, snackbarHostState, savedCount) }
                    }
                ) {
                    Text("保存到本地")
                }
                OutlinedButton(
                    onClick = {
                        rvaList.clear()
                        rvaList.addAll(defaultRvas)
                        scope.launch { persistRvas(context, rvaList, snackbarHostState, savedCount) }
                    }
                ) {
                    Text("恢复默认")
                }
            }
            Text(
                "提示：LSPosed 同时勾选多个 App 会导致 hook 仅生效于当前进程，请一次只勾选一个目标。",
                modifier = Modifier.padding(top = 8.dp)
            )
        }
    }
}

private fun formatRva(value: Long): String {
    return "0x" + value.toString(16)
}

private suspend fun loadRvasFromCs(
    context: android.content.Context,
    uri: android.net.Uri,
    maxCount: Int
): List<String> = withContext(Dispatchers.IO) {
    val setTextPattern = Regex("""set_Text\s*\(""", RegexOption.IGNORE_CASE)
    val rvaPattern = Regex("""RVA:\s*(0x[0-9a-fA-F]+|\d+)""")
    val resolver = context.contentResolver
    val input = resolver.openInputStream(uri) ?: return@withContext emptyList<String>()
    input.use { stream ->
        BufferedReader(InputStreamReader(stream)).use { reader ->
            val lines = reader.readLines()
            val results = LinkedHashSet<String>()
            for (i in 0 until lines.size - 1) {
                val comment = lines[i]
                val next = lines[i + 1]
                if (!setTextPattern.containsMatchIn(next)) continue
                val match = rvaPattern.find(comment) ?: continue
                val raw = match.groupValues.getOrNull(1) ?: continue
                val parsed = parseRva(raw) ?: continue
                results.add(formatRva(parsed))
                if (results.size >= maxCount) break
            }
            return@withContext results.toList()
        }
    }
}

private suspend fun persistRvas(
    context: android.content.Context,
    rvaList: List<String>,
    snackbarHostState: SnackbarHostState,
    savedCountState: androidx.compose.runtime.MutableState<Int>
) {
    val cleaned = rvaList.mapNotNull { parseRva(it) }
    if (cleaned.isEmpty()) {
        snackbarHostState.showSnackbar("至少填入一个有效的 RVA")
        return
    }
    HookConfigStore.saveRvas(context, cleaned)
    savedCountState.value = cleaned.size
    snackbarHostState.showSnackbar("已保存 ${cleaned.size} 个 RVA")
}

@Preview(showBackground = true)
@Composable
fun HookConfigScreenPreview() {
    TextExtractToolTheme {
        HookConfigScreen()
    }
}

private fun parseRva(input: String): Long? {
    val value = input.trim()
    if (value.isEmpty()) return null
    return if (value.startsWith("0x", ignoreCase = true)) {
        value.removePrefix("0x").removePrefix("0X").toLongOrNull(16)
    } else {
        value.toLongOrNull(10)
    }
}
