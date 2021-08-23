#ifndef CROWN_LEGACYCALLS_H
#define CROWN_LEGACYCALLS_H

class uint256;

int GetIXConfirmations(uint256 nTXHash);
int GetTransactionAge(const uint256& txid);

#endif // CROWN_LEGACYCALLS_H
