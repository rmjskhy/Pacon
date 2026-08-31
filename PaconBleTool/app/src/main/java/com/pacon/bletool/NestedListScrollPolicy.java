package com.pacon.bletool;

/** Decides whether a nested scrolling list should retain a vertical drag. */
final class NestedListScrollPolicy {
    private NestedListScrollPolicy() {}

    static boolean disallowParentIntercept(float previousY, float currentY,
                                             boolean canScrollUp,
                                             boolean canScrollDown) {
        if (currentY < previousY) return canScrollDown;
        if (currentY > previousY) return canScrollUp;
        return true;
    }
}