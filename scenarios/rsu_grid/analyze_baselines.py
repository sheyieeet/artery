import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import os
from io import StringIO

def load_data():
    files = {
        'cpm_metrics_base0_original.csv': 'Original',
        'cpm_metrics_base1_static.csv': 'Static 10Hz',
        'cpm_metrics_base2_ETSI.csv': 'ETSI DRM',
        'cpm_metrics_base3_waterfilling_distance.csv': 'Cap-Distance',
        'cpm_metrics_base4_waterfilling_risk.csv': 'Cap-Risk'
    }
    
    dataframes = []
    
    for filename, label in files.items():
        if os.path.exists(filename):
            try:
                with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read().replace('\x00', '')
                df = pd.read_csv(StringIO(content))
                df['Scenario'] = label
                dataframes.append(df)
            except Exception as e:
                print(f"Error loading {filename}: {e}")
        else:
            print(f"File {filename} not found, skipping.")
            
    if not dataframes:
        return None
        
    return pd.concat(dataframes, ignore_index=True)

def main():
    df = load_data()
    if df is None:
        print("No data loaded.")
        return
        
    agg_df = df.groupby(['Scenario', 'Metric'])['Value'].mean().reset_index()
    
    # Adjust ChannelLoad to percentage if it appears to be a ratio
    channel_load = agg_df[agg_df['Metric'] == 'ChannelLoad'].copy()
    if not channel_load.empty and channel_load['Value'].max() <= 1.0:
        agg_df.loc[agg_df['Metric'] == 'ChannelLoad', 'Value'] = channel_load['Value'] * 100
        
    order = ['Original', 'Static 10Hz', 'ETSI DRM', 'Cap-Distance', 'Cap-Risk']
    present_scenarios = [s for s in order if s in agg_df['Scenario'].values]
    
    sns.set_theme(style="whitegrid")
    # Avoid warning: Passing `palette` without assigning `hue` is deprecated
    # So we use hue='Scenario' and legend=False
    
    fig, axes = plt.subplots(1, 3, figsize=(18, 6))
    
    metrics = [
        ('TX_Throughput', 'Average TX Throughput (bytes/s)'),
        ('ChannelLoad', 'Average Channel Load (%)'),
        ('TX_ObjectCount', 'Average TX Object Count per CPM')
    ]
    
    palette = sns.color_palette("colorblind", len(present_scenarios))
    
    for i, (metric, title) in enumerate(metrics):
        ax = axes[i]
        subset = agg_df[agg_df['Metric'] == metric]
        
        if not subset.empty:
            sns.barplot(data=subset, x='Scenario', y='Value', order=present_scenarios, 
                        hue='Scenario', palette=palette, ax=ax, legend=False)
            ax.set_title(title, fontsize=14, pad=10)
            ax.set_ylabel(title, fontsize=12)
            ax.set_xlabel('Scenario', fontsize=12)
            ax.tick_params(axis='x', rotation=45, labelsize=11)
            
            for container in ax.containers:
                # Add bar labels with reasonable precision
                # We can handle format conditionally based on magnitude
                max_val = subset['Value'].max()
                fmt = '%.0f' if max_val > 100 else '%.2f'
                ax.bar_label(container, fmt=fmt, label_type='edge', padding=3)
    
    plt.tight_layout()
    plt.savefig('baseline_comparison.png', dpi=300, bbox_inches='tight')
    print("Saved baseline_comparison.png")

if __name__ == "__main__":
    main()
