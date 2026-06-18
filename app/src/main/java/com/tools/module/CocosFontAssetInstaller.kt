package com.tools.module

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Typeface
import com.tools.il2fusion.config.CocosBundledFont
import de.robv.android.xposed.XposedBridge
import java.io.File
import java.util.Locale
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

object CocosFontAssetInstaller {
    private const val TAG = "[il2Fusion]"
    private const val MODULE_PKG = "com.tools.il2fusion"
    private const val TARGET_DATA_ROOT = "/data/data"
    private const val BM_FONT_SIZE_PX = 40f
    private const val BM_FONT_PADDING_PX = 2
    private const val BM_FONT_SPACING_PX = 1

    data class InstalledFont(
        val ttfPath: String = "",
        val bmfontFntPath: String = ""
    )

    fun installSelectedFont(
        targetContext: Context,
        fontId: String
    ): InstalledFont {
        val font = CocosBundledFont.fromId(fontId) ?: return InstalledFont()

        return try {
            val moduleContext = targetContext.createPackageContext(
                MODULE_PKG,
                Context.CONTEXT_IGNORE_SECURITY
            )
            val targetRoot = File(TARGET_DATA_ROOT, targetContext.packageName)
            if (!targetRoot.isDirectory && !targetRoot.mkdirs()) {
                XposedBridge.log("$TAG bundled font install failed: mkdir ${targetRoot.absolutePath}")
                return InstalledFont()
            }

            val ttfPath = installFont(moduleContext, targetRoot, font)
            val bmfontPath = ""
            InstalledFont(ttfPath = ttfPath, bmfontFntPath = bmfontPath)
        } catch (t: Throwable) {
            XposedBridge.log("$TAG bundled font install failed: $t")
            InstalledFont()
        }
    }

    private fun installFont(
        moduleContext: Context,
        targetRoot: File,
        font: CocosBundledFont
    ): String {
        val bytes = moduleContext.assets.open(font.assetPath).use { input ->
            input.readBytes()
        }
        val outputFile = File(targetRoot, font.fileName)
        val installedSize = copyIfNeeded(bytes, outputFile)
        if (installedSize <= 0L) {
            return ""
        }
        XposedBridge.log(
            "$TAG bundled font installed ${font.id} -> ${outputFile.absolutePath}, " +
                "size=$installedSize"
        )
        return outputFile.absolutePath
    }

    private data class FontPaint(
        val id: String,
        val paint: Paint
    )

    private data class GlyphSpec(
        val codePoint: Int,
        val text: String,
        val paint: Paint,
        val bounds: Rect,
        val width: Int,
        val height: Int,
        val xOffset: Int,
        val yOffset: Int,
        val xAdvance: Int
    )

    private data class PackedGlyph(
        val glyph: GlyphSpec,
        val x: Int,
        val y: Int
    )

    private fun installGeneratedBmFont(
        moduleContext: Context,
        targetRoot: File,
        font: CocosBundledFont,
        seedText: String
    ): String {
        val fontPaints = buildFontPaints(moduleContext, font)
        if (fontPaints.isEmpty()) {
            XposedBridge.log("$TAG bmfont install failed: no usable typeface for ${font.id}")
            return ""
        }

        val codePoints = buildBmFontCodePoints(seedText)
        val glyphs = mutableListOf<GlyphSpec>()
        var missing = 0
        for (codePoint in codePoints) {
            val glyph = buildGlyphSpec(codePoint, fontPaints)
            if (glyph == null) {
                ++missing
            } else {
                glyphs.add(glyph)
            }
        }
        if (glyphs.isEmpty()) {
            XposedBridge.log("$TAG bmfont install failed: no glyphs generated for ${font.id}")
            return ""
        }

        val atlas = packGlyphs(glyphs)
        if (atlas == null) {
            XposedBridge.log("$TAG bmfont install failed: atlas overflow glyphs=${glyphs.size}")
            return ""
        }

        val baseName = "il2fusion_bmfont_${font.id}"
        val pngFile = File(targetRoot, "$baseName.png")
        val fntFile = File(targetRoot, "$baseName.fnt")
        if (!writeBmFontPng(atlas.first, atlas.second, pngFile)) {
            return ""
        }
        val fntText = buildFntText(
            pageFileName = pngFile.name,
            atlasSize = atlas.first,
            glyphs = atlas.second
        )
        if (!writeStringAtomic(fntFile, fntText)) {
            return ""
        }

        XposedBridge.log(
            "$TAG generated bmfont installed ${font.id} -> ${fntFile.absolutePath}, " +
                "png=${pngFile.name}, glyphs=${glyphs.size}, missing=$missing, atlas=${atlas.first}"
        )
        return fntFile.absolutePath
    }

    private fun buildFontPaints(
        moduleContext: Context,
        selectedFont: CocosBundledFont
    ): List<FontPaint> {
        val orderedFonts = buildList {
            add(selectedFont)
            CocosBundledFont.entries.forEach { font ->
                if (font != selectedFont) {
                    add(font)
                }
            }
        }
        val seenAssets = mutableSetOf<String>()
        return orderedFonts.mapNotNull { font ->
            if (!seenAssets.add(font.assetPath)) {
                return@mapNotNull null
            }
            try {
                val typeface = Typeface.createFromAsset(moduleContext.assets, font.assetPath)
                FontPaint(
                    id = font.id,
                    paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                        color = Color.WHITE
                        textSize = BM_FONT_SIZE_PX
                        this.typeface = typeface
                        isSubpixelText = true
                        isDither = true
                    }
                )
            } catch (t: Throwable) {
                XposedBridge.log("$TAG bmfont load typeface failed ${font.id}: $t")
                null
            }
        }
    }

    private fun buildBmFontCodePoints(seedText: String): LinkedHashSet<Int> {
        val codePoints = linkedSetOf<Int>()
        for (codePoint in 0x20..0x7E) {
            codePoints.add(codePoint)
        }
        for (codePoint in 0xA0..0xFF) {
            codePoints.add(codePoint)
        }
        for (codePoint in 0x0E00..0x0E7F) {
            codePoints.add(codePoint)
        }
        for (codePoint in 0x2010..0x2027) {
            codePoints.add(codePoint)
        }
        for (codePoint in 0x3000..0x303F) {
            codePoints.add(codePoint)
        }
        seedText.codePoints().forEach { codePoint ->
            if (codePoint > 0 && codePoint != '\n'.code && codePoint != '\r'.code) {
                codePoints.add(codePoint)
            }
        }
        return codePoints
    }

    private fun buildGlyphSpec(codePoint: Int, fontPaints: List<FontPaint>): GlyphSpec? {
        val text = String(Character.toChars(codePoint))
        val selectedPaint = when (codePoint) {
            0x20, 0xA0, 0x3000 -> fontPaints.firstOrNull()?.paint
            else -> fontPaints.firstOrNull { it.paint.hasGlyph(text) }?.paint
        } ?: return null

        val bounds = Rect()
        selectedPaint.getTextBounds(text, 0, text.length, bounds)
        val advance = ceil(selectedPaint.measureText(text).toDouble()).toInt()
            .coerceAtLeast(if (isZeroWidthMark(codePoint)) 0 else 1)
        if (bounds.isEmpty) {
            return GlyphSpec(
                codePoint = codePoint,
                text = text,
                paint = selectedPaint,
                bounds = bounds,
                width = 0,
                height = 0,
                xOffset = 0,
                yOffset = 0,
                xAdvance = if (isZeroWidthMark(codePoint)) 0 else advance
            )
        }

        val metrics = selectedPaint.fontMetrics
        val baseline = ceil((-metrics.ascent).toDouble()).toInt()
        val combiningOffset = if (isThaiCombiningMark(codePoint)) {
            -ceil((BM_FONT_SIZE_PX * 0.45f).toDouble()).toInt()
        } else {
            Int.MAX_VALUE
        }
        return GlyphSpec(
            codePoint = codePoint,
            text = text,
            paint = selectedPaint,
            bounds = Rect(bounds),
            width = bounds.width() + BM_FONT_PADDING_PX * 2,
            height = bounds.height() + BM_FONT_PADDING_PX * 2,
            xOffset = if (combiningOffset == Int.MAX_VALUE) {
                bounds.left - BM_FONT_PADDING_PX
            } else {
                min(bounds.left - BM_FONT_PADDING_PX, combiningOffset)
            },
            yOffset = baseline + bounds.top - BM_FONT_PADDING_PX,
            xAdvance = if (isZeroWidthMark(codePoint)) 0 else advance
        )
    }

    private fun isZeroWidthMark(codePoint: Int): Boolean {
        val type = Character.getType(codePoint)
        return type == Character.NON_SPACING_MARK.toInt() ||
            type == Character.COMBINING_SPACING_MARK.toInt() ||
            isThaiCombiningMark(codePoint)
    }

    private fun isThaiCombiningMark(codePoint: Int): Boolean {
        return codePoint == 0x0E31 ||
            codePoint in 0x0E34..0x0E3A ||
            codePoint in 0x0E47..0x0E4E
    }

    private fun packGlyphs(glyphs: List<GlyphSpec>): Pair<Int, List<PackedGlyph>>? {
        for (atlasSize in intArrayOf(1024, 2048, 4096)) {
            val packed = tryPackGlyphs(glyphs, atlasSize)
            if (packed != null) {
                return atlasSize to packed
            }
        }
        return null
    }

    private fun tryPackGlyphs(glyphs: List<GlyphSpec>, atlasSize: Int): List<PackedGlyph>? {
        val packed = mutableListOf<PackedGlyph>()
        var x = 0
        var y = 0
        var rowHeight = 0
        for (glyph in glyphs.sortedBy { it.codePoint }) {
            if (glyph.width == 0 || glyph.height == 0) {
                packed.add(PackedGlyph(glyph, 0, 0))
                continue
            }
            if (glyph.width > atlasSize || glyph.height > atlasSize) {
                return null
            }
            if (x + glyph.width > atlasSize) {
                x = 0
                y += rowHeight
                rowHeight = 0
            }
            if (y + glyph.height > atlasSize) {
                return null
            }
            packed.add(PackedGlyph(glyph, x, y))
            x += glyph.width + BM_FONT_SPACING_PX
            rowHeight = max(rowHeight, glyph.height + BM_FONT_SPACING_PX)
        }
        return packed
    }

    private fun writeBmFontPng(
        atlasSize: Int,
        glyphs: List<PackedGlyph>,
        outputFile: File
    ): Boolean {
        val bitmap = Bitmap.createBitmap(atlasSize, atlasSize, Bitmap.Config.ARGB_8888)
        return try {
            bitmap.eraseColor(Color.TRANSPARENT)
            val canvas = Canvas(bitmap)
            for (packed in glyphs) {
                val glyph = packed.glyph
                if (glyph.width == 0 || glyph.height == 0) {
                    continue
                }
                canvas.drawText(
                    glyph.text,
                    packed.x + BM_FONT_PADDING_PX - glyph.bounds.left.toFloat(),
                    packed.y + BM_FONT_PADDING_PX - glyph.bounds.top.toFloat(),
                    glyph.paint
                )
            }
            val parent = outputFile.parentFile
            if (parent != null && !parent.isDirectory && !parent.mkdirs()) {
                XposedBridge.log("$TAG bmfont install failed: mkdir ${parent.absolutePath}")
                return false
            }
            val tmpFile = File(parent, "${outputFile.name}.tmp")
            tmpFile.outputStream().use { output ->
                if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) {
                    XposedBridge.log("$TAG bmfont install failed: png compress ${outputFile.name}")
                    tmpFile.delete()
                    return false
                }
                output.flush()
            }
            replaceAtomic(tmpFile, outputFile)
        } catch (t: Throwable) {
            XposedBridge.log("$TAG bmfont install failed: write png ${outputFile.name}: $t")
            false
        } finally {
            bitmap.recycle()
        }
    }

    private fun buildFntText(
        pageFileName: String,
        atlasSize: Int,
        glyphs: List<PackedGlyph>
    ): String {
        val metrics = glyphs.firstOrNull()?.glyph?.paint?.fontMetrics
        val lineHeight = if (metrics != null) {
            ceil((metrics.descent - metrics.ascent + metrics.leading).toDouble()).toInt()
        } else {
            BM_FONT_SIZE_PX.roundToInt()
        }.coerceAtLeast(BM_FONT_SIZE_PX.roundToInt())
        val base = if (metrics != null) {
            ceil((-metrics.ascent).toDouble()).toInt()
        } else {
            (BM_FONT_SIZE_PX * 0.8f).roundToInt()
        }

        return buildString {
            append("info face=\"il2FusionBMFont\" size=${BM_FONT_SIZE_PX.roundToInt()} bold=0 italic=0 charset=\"\" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=$BM_FONT_SPACING_PX,$BM_FONT_SPACING_PX outline=0\n")
            append("common lineHeight=$lineHeight base=$base scaleW=$atlasSize scaleH=$atlasSize pages=1 packed=0 alphaChnl=1 redChnl=0 greenChnl=0 blueChnl=0\n")
            append("page id=0 file=\"${escapeFntString(pageFileName)}\"\n")
            append("chars count=${glyphs.size}\n")
            glyphs.sortedBy { it.glyph.codePoint }.forEach { packed ->
                val glyph = packed.glyph
                append(
                    String.format(
                        Locale.US,
                        "char id=%d x=%d y=%d width=%d height=%d xoffset=%d yoffset=%d xadvance=%d page=0 chnl=15\n",
                        glyph.codePoint,
                        packed.x,
                        packed.y,
                        glyph.width,
                        glyph.height,
                        glyph.xOffset,
                        glyph.yOffset,
                        glyph.xAdvance
                    )
                )
            }
            append("kernings count=0\n")
        }
    }

    private fun escapeFntString(value: String): String {
        return value.replace("\\", "\\\\").replace("\"", "\\\"")
    }

    private fun writeStringAtomic(outputFile: File, value: String): Boolean {
        return try {
            val bytes = value.toByteArray(Charsets.UTF_8)
            val parent = outputFile.parentFile
            if (parent != null && !parent.isDirectory && !parent.mkdirs()) {
                XposedBridge.log("$TAG bmfont install failed: mkdir ${parent.absolutePath}")
                return false
            }
            val tmpFile = File(parent, "${outputFile.name}.tmp")
            tmpFile.outputStream().use { output ->
                output.write(bytes)
                output.flush()
            }
            replaceAtomic(tmpFile, outputFile)
        } catch (t: Throwable) {
            XposedBridge.log("$TAG bmfont install failed: write fnt ${outputFile.name}: $t")
            false
        }
    }

    private fun replaceAtomic(tmpFile: File, outputFile: File): Boolean {
        if (outputFile.exists() && !outputFile.delete()) {
            tmpFile.delete()
            XposedBridge.log("$TAG bmfont install failed: delete old ${outputFile.absolutePath}")
            return false
        }
        if (!tmpFile.renameTo(outputFile)) {
            tmpFile.delete()
            XposedBridge.log("$TAG bmfont install failed: rename ${tmpFile.absolutePath}")
            return false
        }
        return true
    }

    private fun copyIfNeeded(bytes: ByteArray, outputFile: File): Long {
        if (bytes.isEmpty()) {
            XposedBridge.log("$TAG bundled font install failed: empty asset ${outputFile.name}")
            return 0L
        }
        val parent = outputFile.parentFile
        if (parent != null && !parent.isDirectory && !parent.mkdirs()) {
            XposedBridge.log("$TAG bundled font install failed: mkdir ${parent.absolutePath}")
            return 0L
        }
        if (outputFile.exists() && outputFile.length() == bytes.size.toLong()) {
            return outputFile.length()
        }

        val tmpFile = File(parent, "${outputFile.name}.tmp")
        tmpFile.outputStream().use { output ->
            output.write(bytes)
            output.flush()
        }
        if (tmpFile.length() != bytes.size.toLong()) {
            tmpFile.delete()
            XposedBridge.log("$TAG bundled font install failed: tmp size mismatch ${outputFile.name}")
            return 0L
        }
        if (outputFile.exists() && !outputFile.delete()) {
            tmpFile.delete()
            XposedBridge.log("$TAG bundled font install failed: delete old ${outputFile.absolutePath}")
            return 0L
        }
        if (!tmpFile.renameTo(outputFile)) {
            tmpFile.delete()
            XposedBridge.log("$TAG bundled font install failed: rename ${tmpFile.absolutePath}")
            return 0L
        }
        return outputFile.length()
    }
}
