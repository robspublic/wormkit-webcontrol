"""Authentication helpers.

The user's identity comes solely from a header set by the trusted
oauth-proxy / nginx layer in front of this backend (default X-Auth-Email).
The backend MUST NOT be exposed directly: anything reaching it is assumed to
have passed through the proxy, which strips and re-sets that header so a client
cannot spoof it.
"""

from __future__ import annotations

from fastapi import Depends, Header, HTTPException, status

from .config import settings


def require_email(
    x_auth_email: str | None = Header(default=None, alias="X-Auth-Email"),
) -> str:
    """Return the authenticated user's email, or 401 if the proxy set none."""
    if not x_auth_email:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing authenticated identity",
        )
    return x_auth_email.strip().lower()


def require_admin(email: str = Depends(require_email)) -> str:
    """Allow only configured admin emails (or any user if none configured)."""
    admins = settings.admin_email_set()
    if admins and email not in admins:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Not an administrator",
        )
    return email
