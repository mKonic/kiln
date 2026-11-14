package dev.kiln.billingsample

import dev.kiln.bridge.CalledFromCpp
import dev.kiln.bridge.CppStruct
import dev.kiln.bridge.ImplementedInCpp

// What crosses between Kotlin and C++. tools/gen-jvm writes both sides from this file: BillingCpp
// and PurchaseUpdatesCpp in Kotlin, kiln_bridge.h in C++.

@CppStruct
enum class ProductType { InApp, Subscription }

@CppStruct
data class Product(val id: String, val type: ProductType, val title: String, val description: String, val price: String)

/** Purchase.PurchaseState: a pending purchase is not paid for yet. */
@CppStruct
enum class PurchaseState { Unspecified, Purchased, Pending }

@CppStruct
data class Purchase(val products: List<String>, val token: String, val state: PurchaseState, val acknowledged: Boolean, val orderId: String?)

/** BillingResult: code is BillingClient.BillingResponseCode (0 OK, 1 user cancelled, 3 billing unavailable ...). */
@CppStruct
data class Outcome(val code: Int, val message: String)

@CppStruct
data class Products(val outcome: Outcome, val products: List<Product>)

@CppStruct
data class Purchases(val outcome: Outcome, val purchases: List<Purchase>)

/** Google Play Billing, implemented in Kotlin (PlayBilling.kt) and called from C++ (main.cpp). */
@CalledFromCpp
interface Billing {
    /** Connects to Google Play; the connection is kept and restored by the library. */
    suspend fun connect(): Outcome
    /** The products' details, as the Play Console has them; unknown ids are left out. */
    suspend fun products(ids: List<String>, type: ProductType): Products
    /** Shows Google Play's purchase screen over the app; the result comes to PurchaseUpdates. */
    fun buy(id: String): Outcome
    /** What the user owns now (and pending purchases). */
    suspend fun owned(type: ProductType): Purchases
    /** A non-consumable purchase is the user's for good: acknowledge it within three days. */
    suspend fun acknowledge(token: String): Outcome
    /** A consumable is used up: consuming it lets the user buy it again. */
    suspend fun consume(token: String): Outcome
}

/** Implemented in C++: purchases made (or failed) through the purchase screen. */
@ImplementedInCpp
interface PurchaseUpdates {
    fun updated(outcome: Outcome, purchases: List<Purchase>)
}
