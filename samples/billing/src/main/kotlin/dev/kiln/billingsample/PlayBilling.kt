package dev.kiln.billingsample

import android.app.Activity
import android.content.Context
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.ConsumeParams
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume
import com.android.billingclient.api.Purchase as PlayPurchase

/**
 * Billing over the Play Billing Library, as googlesamples/play-billing-samples' BillingServiceClient
 * uses it: one client with pending purchases and automatic reconnection, product details kept for
 * the purchase screen, purchase updates handed to C++.
 */
class PlayBilling(context: Context, private val activity: () -> Activity?) : Billing {
    private val details = HashMap<String, ProductDetails>()

    private val client: BillingClient = BillingClient.newBuilder(context)
        .setListener { result, purchases -> PurchaseUpdatesCpp.updated(result.outcome(), purchases.orEmpty().map { it.toCpp() }) }
        .enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build())
        .enableAutoServiceReconnection()
        .build()

    override suspend fun connect(): Outcome {
        if (client.isReady) return Outcome(BillingClient.BillingResponseCode.OK, "")
        return suspendCancellableCoroutine { cont ->
            client.startConnection(object : BillingClientStateListener {
                override fun onBillingSetupFinished(result: BillingResult) {
                    if (cont.isActive) cont.resume(result.outcome())
                }
                // enableAutoServiceReconnection reconnects on the next call.
                override fun onBillingServiceDisconnected() {}
            })
        }
    }

    override suspend fun products(ids: List<String>, type: ProductType): Products {
        if (ids.isEmpty()) return Products(Outcome(BillingClient.BillingResponseCode.OK, ""), emptyList())
        val params = QueryProductDetailsParams.newBuilder().setProductList(ids.map {
            QueryProductDetailsParams.Product.newBuilder().setProductId(it).setProductType(type.play()).build()
        }).build()
        val (outcome, found) = suspendCancellableCoroutine<Pair<Outcome, List<ProductDetails>>> { cont ->
            client.queryProductDetailsAsync(params) { result, response ->
                val list = if (result.responseCode == BillingClient.BillingResponseCode.OK) response.productDetailsList else emptyList()
                if (cont.isActive) cont.resume(result.outcome() to list)
            }
        }
        return Products(outcome, found.map { d ->
            details[d.productId] = d
            val price = d.oneTimePurchaseOfferDetails?.formattedPrice
                ?: d.subscriptionOfferDetails?.firstOrNull()?.pricingPhases?.pricingPhaseList?.firstOrNull()?.formattedPrice
                ?: ""
            Product(d.productId, type, d.name, d.description, price)
        })
    }

    override fun buy(id: String): Outcome {
        val d = details[id] ?: return Outcome(BillingClient.BillingResponseCode.ITEM_UNAVAILABLE, "query the product first")
        val a = activity() ?: return Outcome(BillingClient.BillingResponseCode.DEVELOPER_ERROR, "no activity")
        val product = BillingFlowParams.ProductDetailsParams.newBuilder().setProductDetails(d)
        // A subscription is bought through one of its offers (here the first: its base plan).
        d.subscriptionOfferDetails?.firstOrNull()?.let { product.setOfferToken(it.offerToken) }
        val params = BillingFlowParams.newBuilder().setProductDetailsParamsList(listOf(product.build())).build()
        return client.launchBillingFlow(a, params).outcome()
    }

    override suspend fun owned(type: ProductType): Purchases = suspendCancellableCoroutine { cont ->
        client.queryPurchasesAsync(QueryPurchasesParams.newBuilder().setProductType(type.play()).build()) { result, purchases ->
            if (cont.isActive) cont.resume(Purchases(result.outcome(), purchases.map { it.toCpp() }))
        }
    }

    override suspend fun acknowledge(token: String): Outcome = suspendCancellableCoroutine { cont ->
        client.acknowledgePurchase(AcknowledgePurchaseParams.newBuilder().setPurchaseToken(token).build()) { result ->
            if (cont.isActive) cont.resume(result.outcome())
        }
    }

    override suspend fun consume(token: String): Outcome = suspendCancellableCoroutine { cont ->
        client.consumeAsync(ConsumeParams.newBuilder().setPurchaseToken(token).build()) { result, _ ->
            if (cont.isActive) cont.resume(result.outcome())
        }
    }
}

private fun BillingResult.outcome() = Outcome(responseCode, debugMessage)

private fun ProductType.play() = if (this == ProductType.Subscription) BillingClient.ProductType.SUBS else BillingClient.ProductType.INAPP

private fun PlayPurchase.toCpp() = Purchase(
    products, purchaseToken,
    when (purchaseState) {
        PlayPurchase.PurchaseState.PURCHASED -> PurchaseState.Purchased
        PlayPurchase.PurchaseState.PENDING -> PurchaseState.Pending
        else -> PurchaseState.Unspecified
    },
    isAcknowledged, orderId,
)
