package com.pacon.bletool;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.drawable.Drawable;

/** Small code-drawn icons share the same view box and stroke weight. */
final class CompanionIcon extends Drawable {
    private final int kind;
    private final int size;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

    CompanionIcon(int kind, int color, int size) {
        this.kind = kind;
        this.size = size;
        paint.setColor(color);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.8f);
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
    }

    @Override public void draw(Canvas canvas) {
        canvas.save();
        canvas.translate(getBounds().left, getBounds().top);
        canvas.scale(getBounds().width() / 24f, getBounds().height() / 24f);
        Path path = new Path();
        if (kind == 0) {
            path.moveTo(3, 10); path.lineTo(12, 3); path.lineTo(21, 10);
            path.moveTo(5, 9); path.lineTo(5, 21); path.lineTo(10, 21);
            path.lineTo(10, 14); path.lineTo(14, 14); path.lineTo(14, 21);
            path.lineTo(19, 21); path.lineTo(19, 9);
        } else if (kind == 1) {
            path.moveTo(12, 2); path.cubicTo(9, 7, 5, 10, 5, 15);
            path.cubicTo(5, 24, 19, 24, 19, 15);
            path.cubicTo(19, 10, 15, 7, 12, 2);
            path.moveTo(8, 15); path.quadTo(8, 18, 11, 18);
        } else {
            canvas.drawCircle(6, 10, 2.2f, paint);
            canvas.drawCircle(18, 10, 2.2f, paint);
            path.moveTo(9, 15); path.quadTo(12, 21, 15, 15);
        }
        canvas.drawPath(path, paint);
        canvas.restore();
    }

    @Override public int getIntrinsicWidth() { return size; }
    @Override public int getIntrinsicHeight() { return size; }
    @Override public void setAlpha(int alpha) { paint.setAlpha(alpha); invalidateSelf(); }
    @Override public void setColorFilter(ColorFilter filter) { paint.setColorFilter(filter); invalidateSelf(); }
    @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
}
