#include <mn-pos/stakepointer.h>

bool StakePointer::VerifyCollateralSignOver() const
{
    return pubKeyCollateral.Verify(pubKeyProofOfStake.GetHash(), vchSigCollateralSignOver);
}
