/*
NetworkManagerAbove26.java
Copyright (C) 2019 Belledonne Communications, Grenoble, France

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

package org.linphone.core.tools;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkInfo;
import android.net.NetworkCapabilities;
import android.net.ProxyInfo;
import android.net.NetworkRequest;
import android.os.Build;

import org.linphone.core.tools.AndroidPlatformHelper;

/**
 * Intercept network state changes and update linphone core.
 */
public class NetworkManagerAbove26 implements NetworkManagerInterface {
	private AndroidPlatformHelper mHelper;
	private ConnectivityManager mConnectivityManager;
	private ConnectivityManager.NetworkCallback mNetworkCallback;
	private Network mNetworkAvailable;
    private boolean mWifiOnly;

	public NetworkManagerAbove26(final AndroidPlatformHelper helper, ConnectivityManager cm, boolean wifiOnly) {
		mHelper = helper;
		mConnectivityManager = cm;
		mWifiOnly = wifiOnly;
		mNetworkAvailable = null;
		mNetworkCallback = new ConnectivityManager.NetworkCallback() {
			@Override
			public void onAvailable(Network network) {
				Log.i("[Platform Helper] [Network Manager 26] A network is available: " + mConnectivityManager.getNetworkInfo(network).getType() + ", wifi only is " + (mWifiOnly ? "enabled" : "disabled"));
				if (!mWifiOnly || mConnectivityManager.getNetworkInfo(network).getType() == ConnectivityManager.TYPE_WIFI) {
					mNetworkAvailable = network;
					mHelper.updateNetworkReachability();
				} else {
					Log.i("[Platform Helper] [Network Manager 26] Network isn't wifi and wifi only mode is enabled");
				}
			}

			@Override
			public void onLost(Network network) {
				Log.i("[Platform Helper] [Network Manager 26] A network has been lost");
				if (mNetworkAvailable != null && mNetworkAvailable.equals(network)) {
					mNetworkAvailable = null;
				}
				mHelper.updateNetworkReachability();
			}

			@Override
			public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {
				Log.i("[Platform Helper] [Network Manager 26] onCapabilitiesChanged " + network.toString() + ", " + networkCapabilities.toString());
				mHelper.updateNetworkReachability();
			}

			@Override
			public void onLinkPropertiesChanged(Network network, LinkProperties linkProperties) {
				Log.i("[Platform Helper] [Network Manager 26] onLinkPropertiesChanged " + network.toString() + ", " + linkProperties.toString());
				mHelper.updateDnsServers(linkProperties.getDnsServers());
			}

			@Override
			public void onLosing(Network network, int maxMsToLive) {
				Log.i("[Platform Helper] [Network Manager 26] onLosing " + network.toString());
			}

			@Override
			public void onUnavailable() {
				Log.i("[Platform Helper] [Network Manager 26] onUnavailable");
			}
		};
	}

    public void setWifiOnly(boolean isWifiOnlyEnabled) {
		mWifiOnly = isWifiOnlyEnabled;
		if (mWifiOnly && mNetworkAvailable != null) {
			NetworkInfo networkInfo = mConnectivityManager.getNetworkInfo(mNetworkAvailable);
			if (networkInfo != null && networkInfo.getType() != ConnectivityManager.TYPE_WIFI) {
				Log.i("[Platform Helper] [Network Manager 26] Wifi only mode enabled and current network isn't wifi");
				mNetworkAvailable = null;
			}
		}
	}

	public void registerNetworkCallbacks(Context context) {
		int permissionGranted = context.getPackageManager().checkPermission(Manifest.permission.ACCESS_NETWORK_STATE, context.getPackageName());
		Log.i("[Platform Helper] [Network Manager 26] ACCESS_NETWORK_STATE permission is " + (permissionGranted == PackageManager.PERMISSION_GRANTED ? "granted" : "denied"));
		if (permissionGranted == PackageManager.PERMISSION_GRANTED) {
			mConnectivityManager.registerDefaultNetworkCallback(mNetworkCallback, mHelper.getHandler());
		}
	}

	public void unregisterNetworkCallbacks(Context context) {
		mConnectivityManager.unregisterNetworkCallback(mNetworkCallback);
	}

    public NetworkInfo getActiveNetworkInfo() {
        if (mNetworkAvailable != null) {
			return mConnectivityManager.getNetworkInfo(mNetworkAvailable);
		}

        Network network = mConnectivityManager.getActiveNetwork();
		if (network != null) {
			return mConnectivityManager.getNetworkInfo(network);
		}
		Log.i("[Platform Helper] [Network Manager 26] getActiveNetwork() returned null, using getActiveNetworkInfo() instead");
        return mConnectivityManager.getActiveNetworkInfo();
    }

    public Network getActiveNetwork() {
        if (mNetworkAvailable != null) {
			return mNetworkAvailable;
		}

        return mConnectivityManager.getActiveNetwork();
    }

	public boolean isCurrentlyConnected(Context context) {
		int restrictBackgroundStatus = mConnectivityManager.getRestrictBackgroundStatus();
		if (restrictBackgroundStatus == ConnectivityManager.RESTRICT_BACKGROUND_STATUS_ENABLED) {
			// Device is restricting metered network activity while application is running on background.
			// In this state, application should not try to use the network while running on background, because it would be denied.
			Log.w("[Platform Helper] [Network Manager 26] Device is restricting metered network activity while application is running on background");
			if (mHelper.isInBackground()) {
				Log.w("[Platform Helper] [Network Manager 26] Device is in background, returning false");
				return false;
			}
		}
		return mNetworkAvailable != null;
	}

	public boolean hasHttpProxy(Context context) {
		ProxyInfo proxy = mConnectivityManager.getDefaultProxy();
		if (proxy != null && proxy.getHost() != null){
			Log.i("[Platform Helper] [Network Manager 26] The active network is using an http proxy: " + proxy.toString());
			return true;
		}
		return false;
	}

	public String getProxyHost(Context context) {
		ProxyInfo proxy = mConnectivityManager.getDefaultProxy();
		return proxy.getHost();
	}

	public int getProxyPort(Context context) {
		ProxyInfo proxy = mConnectivityManager.getDefaultProxy();
		return proxy.getPort();
	}
}
