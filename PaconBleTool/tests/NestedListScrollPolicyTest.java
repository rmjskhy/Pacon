package com.pacon.bletool;

public final class NestedListScrollPolicyTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        check(NestedListScrollPolicy.disallowParentIntercept(100f, 70f, false, true),
                "upward swipe must stay with a list that can scroll down");
        check(!NestedListScrollPolicy.disallowParentIntercept(100f, 70f, true, false),
                "upward swipe at the list bottom must return to the parent");
        check(NestedListScrollPolicy.disallowParentIntercept(70f, 100f, true, false),
                "downward swipe must stay with a list that can scroll up");
        check(!NestedListScrollPolicy.disallowParentIntercept(70f, 100f, false, true),
                "downward swipe at the list top must return to the parent");
        check(NestedListScrollPolicy.disallowParentIntercept(90f, 90f, false, false),
                "stationary contact must not be stolen before direction is known");
        System.out.println("Nested media list scroll policy tests passed.");
    }
}