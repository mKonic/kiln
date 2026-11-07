#pragma once

// ContentType (ContentType.android.kt): what a node holds, for autofill, as Android autofill hints
// (View.AUTOFILL_HINT_* and androidx.autofill.HintConstants). Combine with `+`; set on a node's
// semantics as `contentType`.

#include <algorithm>
#include <string>
#include <vector>

namespace kiln::ui {

    struct ContentType {
        std::vector<std::string> hints;

        ContentType operator+(const ContentType& o) const {
            ContentType out = *this;
            for (const auto& h : o.hints)
                if (std::find(out.hints.begin(), out.hints.end(), h) == out.hints.end()) out.hints.push_back(h);
            return out;
        }
        bool operator==(const ContentType&) const = default;

        static ContentType Of(std::string hint) { return { { std::move(hint) } }; }
        static ContentType Username() { return Of("username"); }
        static ContentType Password() { return Of("password"); }
        static ContentType EmailAddress() { return Of("emailAddress"); }
        static ContentType NewUsername() { return Of("newUsername"); }
        static ContentType NewPassword() { return Of("newPassword"); }
        static ContentType PostalAddress() { return Of("postalAddress"); }
        static ContentType PostalCode() { return Of("postalCode"); }
        static ContentType CreditCardNumber() { return Of("creditCardNumber"); }
        static ContentType CreditCardSecurityCode() { return Of("creditCardSecurityCode"); }
        static ContentType CreditCardExpirationDate() { return Of("creditCardExpirationDate"); }
        static ContentType CreditCardExpirationMonth() { return Of("creditCardExpirationMonth"); }
        static ContentType CreditCardExpirationYear() { return Of("creditCardExpirationYear"); }
        static ContentType CreditCardExpirationDay() { return Of("creditCardExpirationDay"); }
        static ContentType AddressCountry() { return Of("addressCountry"); }
        static ContentType AddressRegion() { return Of("addressRegion"); }
        static ContentType AddressLocality() { return Of("addressLocality"); }
        static ContentType AddressStreet() { return Of("streetAddress"); }
        static ContentType AddressAuxiliaryDetails() { return Of("extendedAddress"); }
        static ContentType PostalCodeExtended() { return Of("extendedPostalCode"); }
        static ContentType PersonFullName() { return Of("personName"); }
        static ContentType PersonFirstName() { return Of("personGivenName"); }
        static ContentType PersonLastName() { return Of("personFamilyName"); }
        static ContentType PersonMiddleName() { return Of("personMiddleName"); }
        static ContentType PersonMiddleInitial() { return Of("personMiddleInitial"); }
        static ContentType PersonNamePrefix() { return Of("personNamePrefix"); }
        static ContentType PersonNameSuffix() { return Of("personNameSuffix"); }
        static ContentType PhoneNumber() { return Of("phoneNumber"); }
        static ContentType PhoneNumberDevice() { return Of("phoneNumberDevice"); }
        static ContentType PhoneCountryCode() { return Of("phoneCountryCode"); }
        static ContentType PhoneNumberNational() { return Of("phoneNational"); }
        static ContentType Gender() { return Of("gender"); }
        static ContentType BirthDateFull() { return Of("birthDateFull"); }
        static ContentType BirthDateDay() { return Of("birthDateDay"); }
        static ContentType BirthDateMonth() { return Of("birthDateMonth"); }
        static ContentType BirthDateYear() { return Of("birthDateYear"); }
        static ContentType SmsOtpCode() { return Of("smsOTPCode"); }
    };

}
