package com.tools.il2fusion.feature.settings

import android.app.Activity
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Switch
import androidx.compose.material3.Button
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.tools.il2fusion.BuildConfig
import com.tools.il2fusion.R
import com.tools.il2fusion.core.common.TimeFormatter
import com.tools.il2fusion.config.TargetSession
import com.tools.il2fusion.config.TargetSessionStatus
import com.tools.il2fusion.config.TextDbExportJson
import com.tools.il2fusion.core.design.FeatureScreenSurface
import com.tools.il2fusion.core.design.SectionCard
import com.tools.il2fusion.core.i18n.AppLanguage
import com.tools.il2fusion.core.i18n.LanguageMode
import java.io.File

@Composable
fun SettingsRoute(
    modifier: Modifier = Modifier,
    viewModel: SettingsViewModel = viewModel()
) {
    val state = viewModel.uiState.collectAsStateWithLifecycle().value
    val context = LocalContext.current
    val exportStatus = state.textDbExportStatus

    LaunchedEffect(viewModel) {
        viewModel.events.collect { message ->
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }

    LaunchedEffect(
        state.autoShareTextDbExport,
        state.pendingAutoShareTextDbExportRequestId,
        exportStatus.requestId,
        exportStatus.state,
        exportStatus.exportPath
    ) {
        if (state.autoShareTextDbExport &&
            state.pendingAutoShareTextDbExportRequestId.isNotBlank() &&
            exportStatus.state == TextDbExportJson.STATE_SUCCESS &&
            exportStatus.exportPath.isNotBlank() &&
            exportStatus.requestId == state.pendingAutoShareTextDbExportRequestId
        ) {
            viewModel.onTextDbExportAutoShareConsumed(exportStatus.requestId)
            shareTextDbExport(context, exportStatus.exportPath)?.let { message ->
                Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
            }
        }
    }

    FeatureScreenSurface(modifier = modifier) {
        LanguageSection(
            state = state,
            onModeSelected = viewModel::onLanguageModeSelected,
            onLanguageSelected = viewModel::onManualLanguageSelected
        )
        TextDbExportSection(
            state = state,
            onTargetChanged = viewModel::onTextDbExportTargetChanged,
            onExport = viewModel::requestTextDbExport,
            onRefresh = viewModel::refreshTextDbExportStatus,
            onAutoShareChanged = viewModel::onAutoShareTextDbExportChanged,
            onTargetSessionSelected = viewModel::onTargetSessionSelected,
            onShareExport = {
                shareTextDbExport(context, state.textDbExportStatus.exportPath)?.let { message ->
                    Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
                }
            }
        )
        VersionSection(
            state = state,
            onCheckUpdate = viewModel::checkForUpdates,
            onDownloadInstall = viewModel::downloadAndInstallUpdate
        )
    }
}

@Composable
@OptIn(ExperimentalLayoutApi::class)
private fun TextDbExportSection(
    state: SettingsUiState,
    onTargetChanged: (String) -> Unit,
    onExport: () -> Unit,
    onRefresh: () -> Unit,
    onAutoShareChanged: (Boolean) -> Unit,
    onTargetSessionSelected: (TargetSession) -> Unit,
    onShareExport: () -> Unit
) {
    SectionCard(
        title = stringResource(R.string.settings_text_db_export_title),
        subtitle = stringResource(R.string.settings_text_db_export_subtitle)
    ) {
        TargetSessionPicker(
            state = state,
            onTargetSessionSelected = onTargetSessionSelected
        )
        OutlinedTextField(
            value = state.textDbExportTargetPackage,
            onValueChange = onTargetChanged,
            label = { Text(text = stringResource(R.string.settings_text_db_export_target_package)) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = stringResource(R.string.settings_text_db_export_auto_share),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface
            )
            Switch(
                checked = state.autoShareTextDbExport,
                onCheckedChange = onAutoShareChanged
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Button(
                onClick = onExport,
                modifier = Modifier.weight(1f)
            ) {
                Text(text = stringResource(R.string.settings_text_db_export_action))
            }
            TextButton(onClick = onRefresh) {
                Text(text = stringResource(R.string.settings_text_db_export_refresh))
            }
        }
        val status = state.textDbExportStatus
        if (status.requestId.isNotBlank()) {
            Text(
                text = stringResource(
                    R.string.settings_text_db_export_status,
                    exportStateLabel(status.state),
                    status.targetPackage,
                    status.message.ifBlank { "-" }
                ),
                style = MaterialTheme.typography.bodySmall,
                color = when (status.state) {
                    TextDbExportJson.STATE_SUCCESS -> MaterialTheme.colorScheme.primary
                    TextDbExportJson.STATE_FAILURE -> MaterialTheme.colorScheme.error
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                }
            )
            if (status.exportPath.isNotBlank()) {
                Text(
                    text = stringResource(R.string.settings_text_db_export_path, status.exportPath),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (status.state == TextDbExportJson.STATE_SUCCESS && status.exportPath.isNotBlank()) {
                TextButton(onClick = onShareExport) {
                    Text(text = stringResource(R.string.settings_text_db_export_share))
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TargetSessionPicker(
    state: SettingsUiState,
    onTargetSessionSelected: (TargetSession) -> Unit
) {
    val now = state.targetSessionNowMillis.takeIf { it > 0L } ?: System.currentTimeMillis()
    Text(
        text = stringResource(R.string.settings_target_sessions_title),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.onSurface
    )
    if (state.targetSessions.isEmpty()) {
        Text(
            text = stringResource(R.string.settings_target_sessions_empty),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        return
    }
    FlowRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        state.targetSessions.forEach { session ->
            val status = session.statusAt(now)
            FilterChip(
                selected = state.textDbExportTargetPackage == session.packageName,
                onClick = { onTargetSessionSelected(session) },
                label = {
                    Text(
                        text = stringResource(
                            R.string.settings_target_session_chip,
                            session.packageName,
                            targetSessionStatusLabel(status)
                        ),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            )
        }
    }
    state.targetSessions
        .firstOrNull { it.packageName == state.textDbExportTargetPackage }
        ?.let { session ->
            val status = session.statusAt(now)
            Text(
                text = stringResource(
                    R.string.settings_target_session_detail,
                    session.processName,
                    session.pid,
                    targetSessionStatusLabel(status),
                    if (session.textDbExists) stringResource(R.string.common_enabled) else stringResource(R.string.common_disabled),
                    if (session.dumpCsExists) stringResource(R.string.common_enabled) else stringResource(R.string.common_disabled)
                ),
                style = MaterialTheme.typography.bodySmall,
                color = when (status) {
                    TargetSessionStatus.Online -> MaterialTheme.colorScheme.primary
                    TargetSessionStatus.Stale -> MaterialTheme.colorScheme.tertiary
                    TargetSessionStatus.Offline -> MaterialTheme.colorScheme.onSurfaceVariant
                }
            )
        }
}

@Composable
private fun targetSessionStatusLabel(status: TargetSessionStatus): String {
    return when (status) {
        TargetSessionStatus.Online -> stringResource(R.string.settings_target_session_online)
        TargetSessionStatus.Stale -> stringResource(R.string.settings_target_session_stale)
        TargetSessionStatus.Offline -> stringResource(R.string.settings_target_session_offline)
    }
}

private fun shareTextDbExport(context: Context, exportPath: String): String? {
    val file = File(exportPath)
    if (!file.exists() || !file.isFile) {
        return context.getString(R.string.settings_text_db_export_share_missing)
    }
    return runCatching {
        val uri = FileProvider.getUriForFile(
            context,
            "${BuildConfig.APPLICATION_ID}.fileprovider",
            file
        )
        val shareIntent = Intent(Intent.ACTION_SEND).apply {
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_STREAM, uri)
            clipData = ClipData.newUri(context.contentResolver, file.name, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val chooser = Intent.createChooser(
            shareIntent,
            context.getString(R.string.settings_text_db_export_share_title)
        )
        if (context !is Activity) {
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(chooser)
        null
    }.getOrElse { throwable ->
        context.getString(
            R.string.settings_text_db_export_share_failed,
            throwable.message ?: throwable.javaClass.simpleName
        )
    }
}

@Composable
private fun exportStateLabel(state: String): String {
    return when (state) {
        TextDbExportJson.STATE_PENDING -> stringResource(R.string.settings_text_db_export_state_pending)
        TextDbExportJson.STATE_RUNNING -> stringResource(R.string.settings_text_db_export_state_running)
        TextDbExportJson.STATE_SUCCESS -> stringResource(R.string.settings_text_db_export_state_success)
        TextDbExportJson.STATE_FAILURE -> stringResource(R.string.settings_text_db_export_state_failure)
        else -> state.ifBlank { "-" }
    }
}

@Composable
private fun LanguageSection(
    state: SettingsUiState,
    onModeSelected: (LanguageMode) -> Unit,
    onLanguageSelected: (AppLanguage) -> Unit
) {
    SectionCard(
        title = stringResource(R.string.settings_language_title),
        subtitle = stringResource(R.string.settings_language_subtitle)
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            FilterChip(
                selected = state.languageMode == LanguageMode.Auto,
                onClick = { onModeSelected(LanguageMode.Auto) },
                label = { Text(text = stringResource(R.string.settings_language_auto)) }
            )
            FilterChip(
                selected = state.languageMode == LanguageMode.Manual,
                onClick = { onModeSelected(LanguageMode.Manual) },
                label = { Text(text = stringResource(R.string.settings_language_manual)) }
            )
        }
        Text(
            text = stringResource(R.string.settings_language_effective, stringResource(state.effectiveLanguage.labelRes)),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        if (state.languageMode == LanguageMode.Manual) {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                AppLanguage.entries.forEach { language ->
                    FilterChip(
                        selected = state.manualLanguage == language,
                        onClick = { onLanguageSelected(language) },
                        label = { Text(text = stringResource(language.labelRes)) }
                    )
                }
            }
        }
    }
}

@Composable
private fun VersionSection(
    state: SettingsUiState,
    onCheckUpdate: () -> Unit,
    onDownloadInstall: () -> Unit
) {
    SectionCard(
        title = stringResource(R.string.settings_about_title),
        subtitle = stringResource(R.string.settings_about_subtitle)
    ) {
        Text(
            text = stringResource(R.string.settings_version_value, state.versionName, state.versionCode),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface
        )
        Button(
            onClick = onCheckUpdate,
            enabled = !state.isCheckingUpdate && !state.isDownloadingUpdate,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(text = stringResource(R.string.settings_check_update))
        }
        state.updateStatusText?.let { status ->
            Text(
                text = status,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        if (state.isDownloadingUpdate) {
            LinearProgressIndicator(
                progress = { state.downloadProgress / 100f },
                modifier = Modifier.fillMaxWidth()
            )
        }
        state.availableUpdate?.let { updateInfo ->
            OutlinedCard(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                colors = CardDefaults.outlinedCardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
                ),
                border = CardDefaults.outlinedCardBorder()
            ) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = updateInfo.releaseTitle.ifBlank { updateInfo.latestVersionName },
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(R.string.settings_update_release_time, TimeFormatter.formatIsoDateTime(updateInfo.publishedAt)),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    if (updateInfo.changelog.isNotBlank()) {
                        Text(
                            text = updateInfo.changelog,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Button(
                        onClick = onDownloadInstall,
                        enabled = !state.isDownloadingUpdate,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(text = stringResource(R.string.settings_download_install))
                    }
                }
            }
        }
    }
}
