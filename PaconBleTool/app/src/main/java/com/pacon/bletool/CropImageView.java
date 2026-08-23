package com.pacon.bletool;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;

/** Drag-and-pinch crop preview for PACON's 475 x 466 round display canvas. */
final class CropImageView extends View {
    private static final float TARGET_ASPECT = 475.0f / 466.0f;
    private final Bitmap source;
    private final Paint imagePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint shadePaint = new Paint();
    private final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ScaleGestureDetector scaleDetector;
    private final RectF cropRect = new RectF();
    private float scale = 1.0f;
    private float minScale = 1.0f;
    private float offsetX;
    private float offsetY;
    private float lastX;
    private float lastY;
    private boolean laidOut;

    CropImageView(Context context, Bitmap source) {
        super(context);
        this.source = source;
        shadePaint.setColor(0x99000000);
        borderPaint.setStyle(Paint.Style.STROKE);
        borderPaint.setStrokeWidth(getResources().getDisplayMetrics().density * 2.0f);
        borderPaint.setColor(Color.WHITE);
        scaleDetector = new ScaleGestureDetector(context,
                new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                    @Override public boolean onScale(ScaleGestureDetector detector) {
                        float oldScale = scale;
                        scale = clamp(scale * detector.getScaleFactor(), minScale, minScale * 8.0f);
                        float ratio = scale / oldScale;
                        offsetX = detector.getFocusX() -
                                (detector.getFocusX() - offsetX) * ratio;
                        offsetY = detector.getFocusY() -
                                (detector.getFocusY() - offsetY) * ratio;
                        constrainImage();
                        invalidate();
                        return true;
                    }
                });
    }

    @Override protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        float margin = getResources().getDisplayMetrics().density * 18.0f;
        float availableWidth = Math.max(1.0f, width - margin * 2.0f);
        float availableHeight = Math.max(1.0f, height - margin * 2.0f);
        float cropWidth = availableWidth;
        float cropHeight = cropWidth / TARGET_ASPECT;
        if (cropHeight > availableHeight) {
            cropHeight = availableHeight;
            cropWidth = cropHeight * TARGET_ASPECT;
        }
        cropRect.set((width - cropWidth) * 0.5f, (height - cropHeight) * 0.5f,
                (width + cropWidth) * 0.5f, (height + cropHeight) * 0.5f);
        minScale = Math.max(cropRect.width() / source.getWidth(),
                cropRect.height() / source.getHeight());
        if (!laidOut) {
            scale = minScale;
            offsetX = (width - source.getWidth() * scale) * 0.5f;
            offsetY = (height - source.getHeight() * scale) * 0.5f;
            laidOut = true;
        } else if (scale < minScale) {
            scale = minScale;
        }
        constrainImage();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.drawColor(Color.rgb(10, 14, 24));
        canvas.save();
        canvas.translate(offsetX, offsetY);
        canvas.scale(scale, scale);
        canvas.drawBitmap(source, 0.0f, 0.0f, imagePaint);
        canvas.restore();

        canvas.drawRect(0, 0, getWidth(), cropRect.top, shadePaint);
        canvas.drawRect(0, cropRect.bottom, getWidth(), getHeight(), shadePaint);
        canvas.drawRect(0, cropRect.top, cropRect.left, cropRect.bottom, shadePaint);
        canvas.drawRect(cropRect.right, cropRect.top, getWidth(), cropRect.bottom, shadePaint);
        canvas.drawOval(cropRect, borderPaint);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        scaleDetector.onTouchEvent(event);
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                lastX = event.getX();
                lastY = event.getY();
                return true;
            case MotionEvent.ACTION_MOVE:
                if (!scaleDetector.isInProgress() && event.getPointerCount() == 1) {
                    float x = event.getX();
                    float y = event.getY();
                    offsetX += x - lastX;
                    offsetY += y - lastY;
                    lastX = x;
                    lastY = y;
                    constrainImage();
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (event.getPointerCount() > 0) {
                    lastX = event.getX(0);
                    lastY = event.getY(0);
                }
                return true;
            default:
                return true;
        }
    }

    Bitmap createCroppedBitmap(int outputWidth, int outputHeight) {
        Bitmap result = Bitmap.createBitmap(outputWidth, outputHeight, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(result);
        float outputScaleX = outputWidth / cropRect.width();
        float outputScaleY = outputHeight / cropRect.height();
        canvas.translate((offsetX - cropRect.left) * outputScaleX,
                (offsetY - cropRect.top) * outputScaleY);
        canvas.scale(scale * outputScaleX, scale * outputScaleY);
        canvas.drawBitmap(source, 0.0f, 0.0f, imagePaint);
        return result;
    }

    private void constrainImage() {
        if (cropRect.isEmpty()) return;
        float imageWidth = source.getWidth() * scale;
        float imageHeight = source.getHeight() * scale;
        float minX = cropRect.right - imageWidth;
        float maxX = cropRect.left;
        float minY = cropRect.bottom - imageHeight;
        float maxY = cropRect.top;
        offsetX = clamp(offsetX, minX, maxX);
        offsetY = clamp(offsetY, minY, maxY);
    }

    private static float clamp(float value, float low, float high) {
        return Math.max(low, Math.min(high, value));
    }
}
